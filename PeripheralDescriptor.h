// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#ifndef _PeripheralDescriptor_h_
#define _PeripheralDescriptor_h_

#include <cassert>
#include <map>
#include <string>
#include "AccessSecondIterator.h"

class Peripheral;
class Properties;
class SystemState;

class PropertyDescriptor {
public:
  enum Type {
    INTEGER,
    STRING,
    PORT,
  };
private:
  Type type;
  std::string name;
public:
  PropertyDescriptor(Type t, const std::string &n) : type(t), name(n) {}
  static PropertyDescriptor integerProperty(const std::string &name);
  static PropertyDescriptor stringProperty(const std::string &name);
  static PropertyDescriptor portProperty(const std::string &name);
  const std::string &getName() const { return name; }
  Type getType() const { return type; }
};

class PeripheralDescriptor {
  std::string name;
  std::map<std::string,PropertyDescriptor> properties;
  typedef Peripheral*(*CreateFunc)(SystemState &,
                                   const Properties &);
  CreateFunc create;
public:
  PeripheralDescriptor(const std::string &n, CreateFunc c) :
    name(n),
    create(c)
  {
  }
  const std::string &getName() const { return name; }
  void addProperty(const PropertyDescriptor &p);
  const PropertyDescriptor *getProperty(const std::string &name) const;
  void createInstance(SystemState &system, const Properties &properties) const {
    (*create)(system, properties);
  }
  typedef AccessSecondIterator<std::map<std::string,PropertyDescriptor>::iterator> iterator;
  iterator properties_begin() { return properties.begin(); }
  iterator properties_end() { return properties.end(); }
};

#endif // _PeripheralDescriptor_h_
