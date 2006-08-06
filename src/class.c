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

#include "class.h"

// Noone should instantiate Object directly. this should be already
// allocated therefore:

inline void Object_Alloc(Object this) {
  this->__class__ = &__Object;
  this->__super__ = NULL;
};

inline void Object_init() {
  Object_Alloc(&__Object);
};

struct Object __Object = {
  .__class__ = &__Object,
  .__super__ = &__Object,
  .__size = sizeof(struct Object)
};

int issubclass(Object obj, Object class) {
  if(!obj) return 0;

  obj = obj->__class__;
  while(1) {
    if(obj == class->__class__)
      return 1;

    obj=obj->__super__;

    if(obj == &__Object) 
      return 0;
  };
};
