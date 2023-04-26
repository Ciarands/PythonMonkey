#include <gtest/gtest.h>
#include <Python.h>
#include <iostream>
#include <string>

#include "include/TypeEnum.hh"
#include "include/PyType.hh"
#include "include/DictType.hh"
#include "include/StrType.hh"
#include "include/IntType.hh"

class DictTypeTests : public ::testing::Test {
protected:
PyObject *dict_type;
PyObject *default_key;
PyObject *default_value;
virtual void SetUp() {
  Py_Initialize();
  dict_type = PyDict_New();
  default_key = Py_BuildValue("s", (char *)"a");
  default_value = Py_BuildValue("i", 10);

  PyDict_SetItem(dict_type, default_key, default_value);

  Py_XINCREF(dict_type);
  Py_XINCREF(default_key);
  Py_XINCREF(default_value);
}

virtual void TearDown() {
  Py_XDECREF(dict_type);
  Py_XDECREF(default_key);
  Py_XDECREF(default_value);
}
};

TEST_F(DictTypeTests, test_dict_type_instance_of_pytype) {

  DictType dict = DictType(dict_type);

  EXPECT_TRUE(dynamic_cast<const PyType *>(&dict) != nullptr);

}

TEST_F(DictTypeTests, test_sets_values_appropriately) {

  DictType dict = DictType(dict_type);

  StrType *key = new StrType((char *)"c");
  IntType *value = new IntType(15);

  dict.set(key, value);

  PyObject *expected = value->getPyObject();
  PyObject *set_value = PyDict_GetItem(dict.getPyObject(), key->getPyObject());

  EXPECT_EQ(set_value, expected);

  delete key;
  delete value;
}

TEST_F(DictTypeTests, test_gets_existing_values_appropriately) {

  DictType dict = DictType(dict_type);

  StrType *key = new StrType((char *)"a");

  auto get_value = dict.get(key);
  PyType *expected = new IntType(default_value);

  PyObject *get_value_object = get_value->getPyObject();

  EXPECT_EQ(get_value_object, default_value);
}

TEST_F(DictTypeTests, test_get_returns_null_when_getting_non_existent_value) {

  DictType dict = DictType(dict_type);

  StrType *key = new StrType((char *)"b");

  EXPECT_EQ(dict.get(key), nullptr);

}