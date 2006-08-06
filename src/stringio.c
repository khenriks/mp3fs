/*
  This code is from the pyflag project:
  http://pyflag.sourceforge.net/
  
  Copyright (C) Michael Cohen (scudette@users.sourceforge.net)
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <string.h>
#include <ctype.h>
#include "stringio.h"
#include "talloc.h"

StringIO StringIO_constructor(StringIO self) {
  //Create a valid buffer to hold the data:
  self->data = talloc(self,char);
  self->size = 0;
  self->readptr=0;
  
  return self;
};

int StringIO_write(StringIO self,char *data, int len) {
  if(self->readptr+len > self->size) {
    self->size = self->readptr + len;
    
    self->data = talloc_realloc_size(self,self->data,self->size+1);
  };
  
  memcpy(self->data+self->readptr,data,len);
  self->readptr+=len;
  
  return len;
};

int StringIO_sprintf(StringIO self, char *fmt, ...) {
  va_list ap;
  char *data;
  int len;
  
  va_start(ap, fmt);
  data = talloc_vasprintf(self, fmt, ap);
  va_end(ap);
  len = strlen(data);
  
  if(self->readptr+len > self->size) {
    self->size = self->readptr + len;    
    self->data = talloc_realloc_size(self,self->data,self->size+1);
  };
  
  memcpy(self->data+self->readptr,data,len);
  self->readptr+=len;
  talloc_free(data);
  return len;
};

int StringIO_read(StringIO self,char *data,int len) {
  if(self->readptr+len > self->size) {
    len = self->size-self->readptr;
  };

  memcpy(data,self->data+self->readptr,len);
  self->readptr+=len;
  return(len);
};

/** Writes into ourselves from a stream */
int StringIO_read_stream(StringIO self, StringIO stream, int length) {
  char *data;
  int len;
  
  stream->get_buffer(stream,&data,&len);

  //Only write whats available:
  if(length>len) length=len;

  self->write(self,data,length);

  //Move the input stream by that many bytes:
  stream->seek(stream,length,SEEK_CUR);

  return length;
};

/** Write into a stream from ourself */
int StringIO_write_stream(StringIO self, StringIO stream, int length) {
  return stream->read_stream(stream,self,length);
};

int StringIO_seek(StringIO self,int offset,int whence) {
  switch(whence) {
    // Set the readptr:
  case SEEK_SET:
    self->readptr = offset;
    break;
  case SEEK_CUR:
    self->readptr += offset;
    break;
  case SEEK_END:
    self->readptr = self->size+offset;
    break;
  default:
    //    DEBUG("unknown whence");
    //    RAISE(E_GENERIC,NULL,"Unknown whence");
    break;
  };

  if(self->readptr>self->size) {
    self->data=talloc_realloc_size(self,self->data,self->readptr);
    self->size=self->readptr;
  };

  return self->readptr;
};

int StringIO_eof(StringIO self) {
  return (self->readptr==self->size);
};

void StringIO_get_buffer(StringIO self,char **data, int *len) {
  *data = self->data+self->readptr;
  *len = self->size - self->readptr;
};

void StringIO_truncate(StringIO self,int len) {
  self->size=len;
  if(self->readptr > self->size) 
    self->readptr=self->size;
};

void StringIO_skip(StringIO self, int len) {
  if(len > self->size) 
    len=self->size;

  memmove(self->data, self->data+len, self->size-len);
  self->size -= len;
  self->readptr=0;
};

/* locate a substring. This returns a pointer inside the data
   buffer... */
char *StringIO_find(StringIO self, char *needle) {
  if(self->size < strlen(needle))
    return NULL;
  return strstr(self->data, needle);
};

/* case insensitive version of find */
char *StringIO_ifind(StringIO self, char *needle) {
  int i;
  if(self->size < strlen(needle))
    return NULL;
  for(i=0; i<=self->size-strlen(needle); i++) {
    if(strncasecmp(self->data+i, needle, strlen(needle)) == 0)
      return self->data+i;
  }
  return NULL;
};

void StringIO_destroy(StringIO self) {
  //First free our buffer:
  talloc_free(self->data);
  
  //Now free ourselves:
  talloc_free(self);
};

VIRTUAL(StringIO,Object)
  VMETHOD(Con) = StringIO_constructor;
  VMETHOD(write) = StringIO_write;
  VMETHOD(sprintf) = StringIO_sprintf;
  VMETHOD(read) = StringIO_read;
  VMETHOD(read_stream) = StringIO_read_stream;
  VMETHOD(write_stream) = StringIO_write_stream;
  VMETHOD(seek) = StringIO_seek;
  VMETHOD(get_buffer) = StringIO_get_buffer;
  VMETHOD(eof) = StringIO_eof;
  VMETHOD(truncate) = StringIO_truncate;
  VMETHOD(skip) = StringIO_skip;
  VMETHOD(find) = StringIO_find;
  VMETHOD(ifind) = StringIO_ifind;
  VMETHOD(destroy) = StringIO_destroy;
//These are class attributes - all instantiated objects will have
//these set
  VATTR(size) = 0;
  VATTR(readptr) = 0;
END_VIRTUAL
