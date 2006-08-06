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
