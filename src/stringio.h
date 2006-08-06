#ifndef _STRINGIO_H
#define _STRINGIO_H

#include "class.h"
#include <sys/types.h>

CLASS(StringIO,Object)
  /** This is the size of the internal buffer */
  int size;
  /** Current readptr */
  int readptr;
  char *data;
  
  /** constructor */
  StringIO METHOD(StringIO, Con);

  /** Writes data into the string_io at the current offset, growing the
    string_io if needed **/
  int METHOD(StringIO, write, char *data, int len);

  /** Write a format string into the stringio **/
  int METHOD(StringIO, sprintf, char *fmt, ...);

  /** Reads data from the current string location into the buffer (We
      presume it is large enough. We return how much data was actually
      read */
  int METHOD(StringIO, read, char *data, int len);

  /** These allow us to read and write to StringIOs rather than direct
    buffers */
  int METHOD(StringIO, write_stream, StringIO stream, int length);
  int METHOD(StringIO, read_stream, StringIO stream, int length);

  /** The seek method */
  int METHOD(StringIO, seek, int offset, int whence);

  /** get_buffer: Returns a pointer/length to the buffer (relative to readptr) */
  void METHOD(StringIO, get_buffer, char **data, int *len);

  /** Return true if we are at the end of the file */
  int METHOD(StringIO, eof);

  /** Truncates the end of the stream to this size */
  void METHOD(StringIO, truncate, int len);

  /** Removes the first len bytes from the start of the stream. The
      stream is repositioned at its start */
  void METHOD(StringIO, skip, int len);

  /** find a substring, returns a pointer inside data */
  char *METHOD(StringIO, find, char *string);

  /** case insensitive version of find */
  char *METHOD(StringIO, ifind, char *string);

  /** Destructor */
  void METHOD(StringIO, destroy);
END_CLASS

#endif
