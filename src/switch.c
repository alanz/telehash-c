#include "switch.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "util.h"

// a prime number for the internal hashtable used to track all active hashnames/lines
#define MAXPRIME 4211

switch_t switch_new()
{
  switch_t s = malloc(sizeof (struct switch_struct));
  bzero(s, sizeof(struct switch_struct));
  s->cap = 256; // default cap size
  // create all the buckets
  s->buckets = malloc(256 * sizeof(bucket_t));
  bzero(s->buckets, 256 * sizeof(bucket_t));
  s->index = xht_new(MAXPRIME);
  s->parts = packet_new();
  return s;
}

crypt_t loadkey(char csid, switch_t s, packet_t keys)
{
  char hex[4], *pk, *sk;
  crypt_t c;

  util_hex((unsigned char*)&csid,1,(unsigned char*)hex);
  pk = packet_get_str(keys,hex);
  hex[2] = '_';
  hex[3] = 0;
  sk = packet_get_str(keys,hex);
  if(!pk || !sk) return NULL;
  c = crypt_new(csid, (unsigned char*)pk, strlen(pk));
  if(!c) return NULL;
  if(crypt_private(c, (unsigned char*)sk, strlen(sk)))
  {
    crypt_free(c);
    return NULL;
  }
  
  xht_set(s->index,(const char*)c->csidHex,(void *)c);
  packet_set_str(s->parts,c->csidHex,c->part);
  return c;
}

int switch_init(switch_t s, packet_t keys)
{
  if(!keys) return 1;
  
#ifdef CS_1a
  loadkey(0x1a,s,keys);
#endif
#ifdef CS_2a
  loadkey(0x2a,s,keys);
#endif
#ifdef CS_3a
  loadkey(0x3a,s,keys);
#endif
  
  packet_free(keys);

  if(!s->parts->json) return 1;
  s->id = hn_getparts(s->index, s->parts);
  if(!s->id) return 1;
  return 0;
}

void switch_free(switch_t s)
{
  int i;
  for(i=0;i<=255;i++) if(s->buckets[i]) bucket_free(s->buckets[i]);
  if(s->seeds) bucket_free(s->seeds);
  free(s);
}

void switch_cap(switch_t s, int cap)
{
  s->cap = cap;
}

// add this hashname to our bucket list
void switch_bucket(switch_t s, hn_t hn)
{
  unsigned char bucket = hn_distance(s->id, hn);
  if(!s->buckets[bucket]) s->buckets[bucket] = bucket_new();
  bucket_add(s->buckets[bucket], hn);
  // TODO figure out if there's actually more capacity
  
}

void switch_seed(switch_t s, hn_t hn)
{
  if(!s->seeds) s->seeds = bucket_new();
  bucket_add(s->seeds, hn);
}

packet_t switch_sending(switch_t s)
{
  packet_t p;
  if(!s || !s->out) return NULL;
  p = s->out;
  s->out = p->next;
  if(!s->out) s->last = NULL;
  return p;
}

// internally adds to sending queue
void switch_sendingQ(switch_t s, packet_t p)
{
  packet_t dup;
  if(!p) return;

  // if there's no path, find one or copy to many
  if(!p->out)
  {
    // just being paranoid
    if(!p->to)
    {
      packet_free(p);
      return;
    }

    // if the last path is alive, just use that
    if(path_alive(p->to->last)) p->out = p->to->last;
    else{
      int i;
      // try sending to all paths
      for(i=0; p->to->paths[i]; i++)
      {
        dup = packet_copy(p);
        dup->out = p->to->paths[i];
        switch_sendingQ(s, dup);
      }
      packet_free(p);
      return;   
    }
  }

  // update stats
  p->out->atOut = time(0);

  // add to the end of the queue
  if(s->last)
  {
    s->last->next = p;
    s->last = p;
    return;
  }
  s->last = s->out = p;  
}

// tries to send an open if we haven't
void switch_open(switch_t s, hn_t to, path_t direct)
{
  packet_t open;
  time_t now;

  if(!to) return;

  // don't send too frequently
  now = time(0);
  if(now - to->sentOpen < 2) return;
  to->sentOpen = now;

  // actually send the open
  open = crypt_openize(s->id->c, to->c, packet_new());
  if(!open) return;
  open->to = to;
  if(direct) open->out = direct;
  switch_sendingQ(s, open);
}

void switch_send(switch_t s, packet_t p)
{
  packet_t lined;

  if(!p) return;
  
  // require recipient at least
  if(!p->to) return (void)packet_free(p);

  // encrypt the packet to the line, chains together
  lined = crypt_lineize(s->id->c, p->to->c, p);
  if(lined) return switch_sendingQ(s, lined);

  // queue most recent packet to be sent after opened
  if(p->to->onopen) packet_free(p->to->onopen);
  p->to->onopen = p;

  // no line, so generate open instead
  switch_open(s, p->to, NULL);
}

chan_t switch_pop(switch_t s)
{
  chan_t c;
  if(!s->chans) return NULL;
  c = s->chans;
  s->chans = c->next;
  return c;
}

void switch_receive(switch_t s, packet_t p, path_t in)
{
  hn_t from;
  packet_t line;
  crypt_t opened;

  if(!s || !p || !in) return;
  if(util_cmp("open",packet_get_str(p,"type")) == 0)
  {
    opened = crypt_deopenize(s->id->c, p);
    if(opened)
    {
      from = hn_get(s->index, crypt_hashname(opened));
      from->c = crypt_merge(from->c, opened);
      switch_open(s, from, NULL); // in case we need to send an open
      xht_set(s->index, crypt_line(from->c), from);
      in = hn_path(from, in);
      if(from->onopen)
      {
        packet_t last = from->onopen;
        from->onopen = NULL;
        last->out = in;
        switch_send(s, last);
      }
    }
    return;
  }
  if(util_cmp("line",packet_get_str(p,"type")) == 0)
  {
    from = xht_get(s->index, packet_get_str(p, "line"));
    if(from)
    {
      in = hn_path(from, in);
      line = crypt_delineize(s->id->c, from->c, p);
      if(line)
      {
        chan_t c = chan_in(s, from, line);
        if(c) return chan_receive(c, line);
        // bounce it!
        if(!packet_get_str(line,"err"))
        {
          packet_set_str(line,"err","unknown channel");
          line->to = from;
          line->out = in;
          switch_send(s, line);          
        }else{
          packet_free(line);
        }
      }
      return;
    }
  }
  
  // nothing processed, clean up
  packet_free(p);
}

