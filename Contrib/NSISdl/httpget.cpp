/*
** JNetLib
** Copyright (C) 2000-2001 Nullsoft, Inc.
** Author: Justin Frankel
** File: httpget.cpp - JNL HTTP GET implementation
** License: see License.txt
**
** Unicode support by Jim Park -- 08/24/2007
*/

#include "netinc.h"
#include "util.h"
#include "httpget.h"


JNL_HTTPGet::JNL_HTTPGet(JNL_AsyncDNS *dns, int recvbufsize, char *proxy)
{
  m_recvbufsize=recvbufsize;
  m_dns=dns;
  m_con=NULL;
  m_http_proxylpinfo=0;
  m_http_proxyhost=0;
  m_http_proxyport=0;
  if (proxy && *proxy)
  {
    char *p=(char*)malloc(strlen(proxy)+1);
    if (p) 
    {
      char *r=NULL;
      strcpy(p,proxy);
      do_parse_url(p,&m_http_proxyhost,&m_http_proxyport,&r,&m_http_proxylpinfo);
      free(r);
      free(p);
    }
  }
  m_sendheaders=NULL;
  reinit();
}

void JNL_HTTPGet::reinit()
{
  m_errstr=0;
  m_recvheaders=NULL;
  m_recvheaders_size=0;
  m_http_state=0;
  m_http_port=0;
  m_http_url=0;
  m_reply=0;
  m_http_host=m_http_lpinfo=m_http_request=NULL;
}

void JNL_HTTPGet::deinit()
{
  delete m_con;
  free(m_recvheaders);

  free(m_http_url);
  free(m_http_host);
  free(m_http_lpinfo);
  free(m_http_request);
  free(m_errstr);
  free(m_reply);
  reinit();
}

JNL_HTTPGet::~JNL_HTTPGet()
{
  deinit();
  free(m_sendheaders);
  free(m_http_proxylpinfo);
  free(m_http_proxyhost);

}


void JNL_HTTPGet::addheader(const char *header)
{
  //if (strstr(header,"\r") || strstr(header,"\n")) return;
  if (!m_sendheaders)
  {
    m_sendheaders=(char*)malloc(strlen(header)+3);
    if (m_sendheaders) 
    {
      strcpy(m_sendheaders,header);
      strcat(m_sendheaders,"\r\n");
    }
  }
  else
  {
    char *t=(char*)malloc(strlen(header)+strlen(m_sendheaders)+1+2);
    if (t)
    {
      strcpy(t,m_sendheaders);
      strcat(t,header);
      strcat(t,"\r\n");
      free(m_sendheaders);
      m_sendheaders=t;
    }
  }
}

void JNL_HTTPGet::do_encode_mimestr(char *in, char *out)
{
  char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int shift = 0;
  int accum = 0;

  while (*in)
  {
    if (*in)
    {
      accum <<= 8;
      shift += 8;
      accum |= *in++;
    }
    while ( shift >= 6 )
    {
      shift -= 6;
      *out++ = alphabet[(accum >> shift) & 0x3F];
    }
  }
  if (shift == 4)
  {
    *out++ = alphabet[(accum & 0xF)<<2];
    *out++='=';  
  }
  else if (shift == 2)
  {
    *out++ = alphabet[(accum & 0x3)<<4];
    *out++='=';  
    *out++='=';  
  }

  *out++=0;
}


void JNL_HTTPGet::connect(const char *url)
{
  deinit();
  m_http_url=(char*)malloc(strlen(url)+1);
  strcpy(m_http_url,url);
  do_parse_url(m_http_url,&m_http_host,&m_http_port,&m_http_request, &m_http_lpinfo);
  strcpy(m_http_url,url);
  if (!m_http_host || !m_http_host[0] || !m_http_port)
  {
    m_http_state=-1;
    seterrstr("invalid URL");
    return;
  }

  int sendbufferlen=0;

  if (!m_http_proxyhost || !m_http_proxyhost[0])
  {
    sendbufferlen += 4 /* GET */ + strlen(m_http_request) + 9 /* HTTP/1.0 */ + 2;
  }
  else
  {
    sendbufferlen += 4 /* GET */ + strlen(m_http_url) + 9 /* HTTP/1.0 */ + 2;
    if (m_http_proxylpinfo&&m_http_proxylpinfo[0])
    {
      sendbufferlen+=58+strlen(m_http_proxylpinfo)*2; // being safe here
    }
  }
  sendbufferlen += 5 /* Host: */ + strlen(m_http_host) + 2;

  if (m_http_lpinfo&&m_http_lpinfo[0])
  {
    sendbufferlen+=46+strlen(m_http_lpinfo)*2; // being safe here
  }

  if (m_sendheaders) sendbufferlen+=strlen(m_sendheaders);

  char *str=(char*)malloc(sendbufferlen+1024);
  if (!str)
  {
    seterrstr("error allocating memory");
    m_http_state=-1;    
  }

  if (!m_http_proxyhost || !m_http_proxyhost[0])
  {
    wsprintfA(str,"GET %s HTTP/1.0\r\n",m_http_request);
  }
  else
  {
    wsprintfA(str,"GET %s HTTP/1.0\r\n",m_http_url);
  }

  wsprintfA(str+strlen(str),"Host: %s\r\n",m_http_host);

  if (m_http_lpinfo&&m_http_lpinfo[0])
  {
    strcat(str,"Authorization: Basic ");
    do_encode_mimestr(m_http_lpinfo,str+strlen(str));
    strcat(str,"\r\n");
  }
  if (m_http_proxylpinfo&&m_http_proxylpinfo[0])
  {
    strcat(str,"Proxy-Authorization: Basic ");
    do_encode_mimestr(m_http_proxylpinfo,str+strlen(str));
    strcat(str,"\r\n");
  }

  if (m_sendheaders) strcat(str,m_sendheaders);
  strcat(str,"\r\n");

  int a=m_recvbufsize;
  if (a < 4096) a=4096;
  m_con=new JNL_Connection(m_dns,strlen(str)+4,a);
  if (m_con)
  {
    if (!m_http_proxyhost || !m_http_proxyhost[0])
    {
      m_con->connect(m_http_host,m_http_port);
    }
    else
    {
      m_con->connect(m_http_proxyhost,m_http_proxyport);
    }
    m_con->send_string(str);
  }
  else
  {
    m_http_state=-1;
    seterrstr("could not create connection object");
  }
  free(str);

}

static int my_strnicmp(char *b1, const char *b2, int l)
{
  while (l-- && *b1 && *b2)
  {
    char bb1=*b1++;
    char bb2=*b2++;
    if (bb1>='a' && bb1 <= 'z') bb1+='A'-'a';
    if (bb2>='a' && bb2 <= 'z') bb2+='A'-'a';
    if (bb1 != bb2) return bb1-bb2;
  }
  return 0;
}

char *_strstr(const char *i, const char *s)
{
  if (strlen(i)>=strlen(s)) while (i[strlen(s)-1])
  {
    int l=strlen(s)+1;
    const char *ii=i;
    const char *is=s;
    while (--l>0)
    {
      if (*ii != *is) break;
      ii++;
      is++;
    }
    if (l==0) return const_cast<char*>(i);
    i++;
  }
  return NULL;
}

#define strstr _strstr

void JNL_HTTPGet::do_parse_url(char *url, char **host, int *port, char **req, char **lp)
{
  char *p,*np;
  free(*host); *host=0;
  free(*req); *req=0;
  free(*lp); *lp=0;

  if (strstr(url,"://")) np=p=strstr(url,"://")+3;
  else np=p=url;
  while (*np != '/' && *np) np++;
  if (*np)
  {
    *req=(char*)malloc(strlen(np)+1);
    if (*req) strcpy(*req,np);
    *np++=0;
  } 
  else 
  {
    *req=(char*)malloc(2);
    if (*req) strcpy(*req,"/");
  }

  np=p;
  while (*np != '@' && *np) np++;
  if (*np)
  {
    *np++=0;
    *lp=(char*)malloc(strlen(p)+1);
    if (*lp) strcpy(*lp,p);
    p=np;
  }
  else 
  {
    *lp=(char*)malloc(1);
    if (*lp) strcpy(*lp,"");
  }
  np=p;
  while (*np != ':' && *np) np++;
  if (*np)
  {
    *np++=0;
    *port=my_atoi(np);
  } else *port=80;
  *host=(char*)malloc(strlen(p)+1);
  if (*host) strcpy(*host,p);
}


const char *JNL_HTTPGet::getallheaders()
{ // double null terminated, null delimited list
  if (m_recvheaders) return m_recvheaders;
  else return "\0\0";
}

const char *JNL_HTTPGet::getheader(const char *headername)
{
  char *ret=NULL;
  if (strlen(headername)<1||!m_recvheaders) return NULL;
  char *p=m_recvheaders;
  while (*p)
  {
    if (!my_strnicmp(p,headername,strlen(headername)))
    {
      ret=p+strlen(headername);
      while (*ret == ' ') ret++;
      break;
    }
    p+=strlen(p)+1;
  }
  return ret;
}

int JNL_HTTPGet::run()
{
  int cnt=0;
  if (m_http_state==-1||!m_con) return -1; // error


run_again:
  static char main_buf[4096];
  char *buf = main_buf;
  m_con->run();

  if (m_con->get_state()==JNL_Connection::STATE_ERROR)
  {
    seterrstr(m_con->get_errstr());
    return -1;
  }
  if (m_con->get_state()==JNL_Connection::STATE_CLOSED) return 1;

  if (m_http_state==0) // connected, waiting for reply
  {
    if (m_con->recv_lines_available()>0)
    {
      m_con->recv_line(buf,4095);
      buf[4095]=0;
      m_reply=(char*)malloc(strlen(buf)+1);
      strcpy(m_reply,buf);

      if (strstr(buf,"200")) m_http_state=2; // proceed to read headers normally
      else if (strstr(buf,"301") || strstr(buf,"302")) 
      {
        m_http_state=1; // redirect city
      }
      else 
      {
        seterrstr(buf);
        m_http_state=-1;
        return -1;
      }
      cnt=0;
    }
    else if (!cnt++) goto run_again;
  }
  if (m_http_state == 1) // redirect
  {
    while (m_con->recv_lines_available() > 0)
    {
      m_con->recv_line(buf,4096);
      if (!buf[0])  
      {
        m_http_state=-1;
        return -1;
      }
      if (!my_strnicmp(buf,"Location:",9))
      {
        char *p=buf+9; while (*p== ' ') p++;
        if (*p)
        {
          char newurl[1024];

          lstrcpyA(newurl, p);
          if (my_strnicmp(newurl, "http://", 7))
          {
            char *q;

            // generate full URL from original
            lstrcpyA(newurl, m_http_url);

            if (p[0] == '/')
            {
              // new location is an absolute path
              q=&(newurl[7]);
              while (*q != 0)
              {
                if (*q == '/')
                {
                  *q=0;
                  break;
                }
                q++;
              }
            }
            else
            {
              // new location is an relative path
              q=&(newurl[lstrlenA(newurl) - 1]);
              while (q >= newurl)
              {
                if (*q == '/')
                {
                  *q=0;
                  break;
                }
                q--;
              }

              lstrcatA(newurl, "/");
            }

            lstrcatA(newurl, p);
          }
          connect(newurl);
          return 0;
        }
      }
    }
  }
  if (m_http_state==2)
  {
    if (!cnt++ && m_con->recv_lines_available() < 1) goto run_again;
    while (m_con->recv_lines_available() > 0)
    {
      m_con->recv_line(buf,4096);
      if (!buf[0]) { m_http_state=3; break; }

      char *h = buf;

      // workaround for bug #1445735
      //
      // some proxies, like WinProxy, prefix headers with tabs
      // or spaces. to make sure headers are detected properly,
      // this removes up to 128 useless white spaces.

      while ((h - buf < 128) && (*h == ' ' || *h == '\t')) h++;

      if (!m_recvheaders)
      {
        m_recvheaders_size=strlen(h)+1;
        m_recvheaders=(char*)malloc(m_recvheaders_size+1);
        if (m_recvheaders)
        {
          strcpy(m_recvheaders,h);
          m_recvheaders[m_recvheaders_size]=0;
        }
      }
      else
      {
        int oldsize=m_recvheaders_size;
        m_recvheaders_size+=strlen(h)+1;
        char *n=(char*)malloc(m_recvheaders_size+1);
        if (n)
        {
          memcpy(n,m_recvheaders,oldsize);
          strcpy(n+oldsize,h);
          n[m_recvheaders_size]=0;
          free(m_recvheaders);
          m_recvheaders=n;
        }
      }
    }
  }
  if (m_http_state==3)
  {
  }
  return 0;
}

int JNL_HTTPGet::get_status() // returns 0 if connecting, 1 if reading headers, 
                    // 2 if reading content, -1 if error.
{
  if (m_http_state < 0) return -1;
  if (m_http_state < 2) return 0;
  if (m_http_state == 2) return 1;
  if (m_http_state == 3) return 2;
  return -1;
}

int JNL_HTTPGet::getreplycode()// returns 0 if none yet, otherwise returns http reply code.
{
  if (!m_reply) return 0;
  char *p=m_reply;
  while (*p && *p != ' ') p++; // skip over HTTP/x.x
  if (!*p) return 0;
  return my_atoi(++p);
}

int JNL_HTTPGet::bytes_available()
{
  if (m_con && m_http_state==3) return m_con->recv_bytes_available();
  return 0;
}
int JNL_HTTPGet::get_bytes(char *buf, int len)
{
  if (m_con && m_http_state==3) return m_con->recv_bytes(buf,len);
  return 0;
}
int JNL_HTTPGet::peek_bytes(char *buf, int len)
{
  if (m_con && m_http_state==3) return m_con->peek_bytes(buf,len);
  return 0;
}

__int64 JNL_HTTPGet::content_length()
{
  const char *p=getheader("content-length:");
  if (!p) return 0;
  __int64 cl = myatoi64(p);
  if (cl > 0) return cl;

  // workaround for bug #1744091
  // some buggy apache servers return negative values for sizes
  // over 2gb - fix it for them
  if (cl < 0)
    return (__int64)((unsigned int)(cl));

  return 0;
}
