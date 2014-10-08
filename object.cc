// Copyright 2013-2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// PKCS#11 s11.7: Object management functions
//   C_CreateObject
//   C_CopyObject
//   C_DestroyObject
//   C_GetObjectSize
//   C_GetAttributeValue
//   C_SetAttributeValue
//   C_FindObjectsInit
//   C_FindObjects
//   C_FindObjectsFinal

#include <cstdlib>
#include "pkcs11test.h"

#include <set>
#include <vector>

using namespace std;  // So sue me

namespace pkcs11 {
namespace test {
namespace {

CK_BYTE deadbeef[] = { 0xDE, 0xAD, 0xBE, 0xEF};

set<CK_OBJECT_HANDLE> GetObjects(CK_SESSION_HANDLE session, int stride) {
  set<CK_OBJECT_HANDLE> results;
  EXPECT_CKR_OK(g_fns->C_FindObjectsInit(session, NULL_PTR, 0));
  while (true) {
    vector<CK_OBJECT_HANDLE> objects(stride);
    CK_ULONG object_count;
    EXPECT_CKR_OK(g_fns->C_FindObjects(session, objects.data(), stride, &object_count));
    if (object_count == 0) break;
    for (size_t ii = 0; ii < object_count; ii++) {
      results.insert(objects[ii]);
    }
  }
  EXPECT_CKR_OK(g_fns->C_FindObjectsFinal(session));
  return results;
}

void EnumerateObjects(CK_SESSION_HANDLE session) {
  // First check session state.
  CK_SESSION_INFO session_info;
  EXPECT_CKR_OK(g_fns->C_GetSessionInfo(session, &session_info));

  EXPECT_CKR_OK(g_fns->C_FindObjectsInit(session, NULL_PTR, 0));
  while (true) {
    CK_OBJECT_HANDLE object;
    CK_ULONG object_count;
    EXPECT_CKR_OK(g_fns->C_FindObjects(session, &object, 1, &object_count));
    if (object_count == 0) break;
    CK_ULONG object_size;
    EXPECT_CKR_OK(g_fns->C_GetObjectSize(session, object, &object_size));
    if (g_verbose) cout << "  object x" << setw(8) << setfill('0') << hex << (unsigned int)object
                        << " (size=" << (int)object_size << ")" << endl;
    if (g_verbose) cout << object_description(g_fns, session, object);
    if (session_info.state == CKS_RO_PUBLIC_SESSION || session_info.state == CKS_RW_PUBLIC_SESSION) {
      // Not logged in, so should not see private objects.
      CK_BBOOL is_private;
      CK_ATTRIBUTE get_attr = {CKA_PRIVATE, &is_private, sizeof(is_private)};
      EXPECT_CKR_OK(g_fns->C_GetAttributeValue(session, object, &get_attr, 1));
      EXPECT_EQ(CK_FALSE, is_private);
    }
  }
  EXPECT_CKR_OK(g_fns->C_FindObjectsFinal(session));
}


}  // namespace

TEST_F(ReadOnlySessionTest, EnumerateObjects) {
  EnumerateObjects(session_);
}

TEST_F(ROUserSessionTest, EnumerateObjects) {
  if (!(g_token_flags & CKF_LOGIN_REQUIRED)) {
    TEST_SKIPPED("Login required");
    return;
  }
  EnumerateObjects(session_);
}

TEST_F(ReadWriteSessionTest, EnumerateObjects) {
  EnumerateObjects(session_);
}

TEST_F(RWUserSessionTest, EnumerateObjects) {
  if (!(g_token_flags & CKF_LOGIN_REQUIRED)) {
    TEST_SKIPPED("Login required");
    return;
  }
  EnumerateObjects(session_);
}

TEST_F(ReadOnlySessionTest, ConsistentObjects) {
  // Shouldn't matter whether we retrieve the objects one at a time or in bigger lumps.
  set<CK_OBJECT_HANDLE> objs1 = GetObjects(session_, 1);
  set<CK_OBJECT_HANDLE> objs2 = GetObjects(session_, 10);
  set<CK_OBJECT_HANDLE> objs3 = GetObjects(session_, 1024);
  EXPECT_EQ(objs1, objs2);
  EXPECT_EQ(objs1, objs3);
}

TEST_F(ReadWriteSessionTest, ConsistentObjects) {
  // Shouldn't matter whether we retrieve the objects one at a time or in bigger lumps.
  set<CK_OBJECT_HANDLE> objs1 = GetObjects(session_, 1);
  set<CK_OBJECT_HANDLE> objs2 = GetObjects(session_, 10);
  set<CK_OBJECT_HANDLE> objs3 = GetObjects(session_, 1024);
  EXPECT_EQ(objs1, objs2);
  EXPECT_EQ(objs1, objs3);
}

TEST_F(ReadWriteSessionTest, CreateCopyDestroyObject) {
  // Create a data object.
  CK_OBJECT_CLASS data_class = CKO_DATA;
  CK_UTF8CHAR app[] = "pkcs11test";
  CK_UTF8CHAR label[] = "OldLabel";
  CK_ATTRIBUTE attrs[] = {
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {CKA_TOKEN, &g_ck_false, sizeof(g_ck_false)},  // Session object
    {CKA_APPLICATION, app, sizeof(app)},
    {CKA_VALUE, deadbeef, sizeof(deadbeef)},
    {CKA_LABEL, label, 8},
  };
  CK_ULONG num_attrs = sizeof(attrs) / sizeof(attrs[0]);
  CK_OBJECT_HANDLE object;
  ASSERT_CKR_OK(g_fns->C_CreateObject(session_, attrs, num_attrs, &object));

  CK_ULONG object_size;
  EXPECT_CKR_OK(g_fns->C_GetObjectSize(session_, object, &object_size));

  CK_OBJECT_HANDLE object2;
  EXPECT_CKR_OK(g_fns->C_CopyObject(session_, object, attrs, 0, &object2));

  CK_ULONG object2_size;
  EXPECT_CKR_OK(g_fns->C_GetObjectSize(session_, object, &object2_size));
  EXPECT_EQ(object_size, object2_size);

  // Check each attribute in turn.
  CK_BYTE buffer[256];
  for (size_t ii = 0; ii < num_attrs; ii++) {
    CK_ATTRIBUTE get_attr = {attrs[ii].type, buffer, sizeof(buffer)};
    EXPECT_CKR_OK(g_fns->C_GetAttributeValue(session_, object2, &get_attr, 1));
    EXPECT_EQ(attrs[ii].type, get_attr.type);
    EXPECT_EQ(buffer, get_attr.pValue);
    EXPECT_EQ(attrs[ii].ulValueLen, get_attr.ulValueLen);
    EXPECT_EQ(0, memcmp(buffer, attrs[ii].pValue, attrs[ii].ulValueLen));
  }

  // Check another attribute is absent.
  CK_ATTRIBUTE get_attr = {CKA_CERTIFICATE_TYPE, buffer, sizeof(buffer)};
  EXPECT_CKR(CKR_ATTRIBUTE_TYPE_INVALID,
             g_fns->C_GetAttributeValue(session_, object2, &get_attr, 1));
  EXPECT_EQ((CK_ULONG)-1, get_attr.ulValueLen);

  // Set a new attribute on the original object.
  CK_UTF8CHAR new_label[] = "NewLabel";
  CK_ATTRIBUTE set_attr = {CKA_LABEL, new_label, 8};
  EXPECT_CKR_OK(g_fns->C_SetAttributeValue(session_, object, &set_attr, 1));

  // Unaffected on the copy, changed on the original.
  get_attr.type = CKA_LABEL;
  get_attr.ulValueLen = sizeof(buffer);
  EXPECT_CKR_OK(g_fns->C_GetAttributeValue(session_, object2, &get_attr, 1));
  EXPECT_EQ(8, get_attr.ulValueLen);
  EXPECT_EQ(0, memcmp(label, get_attr.pValue, 5));

  get_attr.ulValueLen = sizeof(buffer);
  EXPECT_CKR_OK(g_fns->C_GetAttributeValue(session_, object, &get_attr, 1));
  EXPECT_EQ(8, get_attr.ulValueLen);
  EXPECT_EQ(0, memcmp(new_label, get_attr.pValue, 5));

  // Make another copy but change the value attribute along the way.
  CK_OBJECT_HANDLE object3;
  CK_BYTE facefeed[] = { 0xFA, 0xCE, 0xFE, 0xED};
  CK_ATTRIBUTE attrs3[] = {
    {CKA_VALUE, facefeed, sizeof(facefeed)},
  };
  EXPECT_CKR_OK(g_fns->C_CopyObject(session_, object, attrs3, 1, &object3));

  CK_ATTRIBUTE get_value = {CKA_VALUE, buffer, sizeof(buffer)};
  EXPECT_CKR_OK(g_fns->C_GetAttributeValue(session_, object3, &get_value, 1));
  EXPECT_EQ(sizeof(facefeed), get_value.ulValueLen);
  EXPECT_EQ(hex_data(facefeed, sizeof(facefeed)), hex_data((CK_BYTE_PTR)get_value.pValue, get_value.ulValueLen));

  EXPECT_CKR_OK(g_fns->C_DestroyObject(session_, object3));
  EXPECT_CKR_OK(g_fns->C_DestroyObject(session_, object2));
  EXPECT_CKR_OK(g_fns->C_DestroyObject(session_, object));
}

TEST_F(ReadWriteSessionTest, CreateObjectInvalid) {
  CK_OBJECT_CLASS data_class = CKO_DATA;
  CK_UTF8CHAR app[] = "pkcs11test";
  CK_ATTRIBUTE attrs[] = {
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {CKA_TOKEN, &g_ck_false, sizeof(g_ck_false)},  // Session object
    {CKA_APPLICATION, app, sizeof(app)},
    {CKA_VALUE, deadbeef, sizeof(deadbeef)},
  };
  CK_OBJECT_HANDLE object;
  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_CreateObject(INVALID_SESSION_HANDLE, attrs, sizeof(attrs)/sizeof(attrs[0]), &object));
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_CreateObject(session_, attrs, sizeof(attrs)/sizeof(attrs[0]), NULL_PTR));
  CK_RV rv = g_fns->C_CreateObject(session_, NULL_PTR, sizeof(attrs)/sizeof(attrs[0]), &object);
  EXPECT_TRUE(rv == CKR_TEMPLATE_INCOMPLETE || rv == CKR_ARGUMENTS_BAD) << " rv=" << CK_RV_(rv);

  rv = g_fns->C_CreateObject(session_, attrs, 0, &object);
  EXPECT_TRUE(rv == CKR_TEMPLATE_INCOMPLETE || rv == CKR_ARGUMENTS_BAD) << " rv=" << CK_RV_(rv);

  CK_ATTRIBUTE attr_value[] = {
    {CKA_VALUE, deadbeef, sizeof(deadbeef)},
  };
  EXPECT_CKR(CKR_TEMPLATE_INCOMPLETE,
             g_fns->C_CreateObject(session_, attr_value, 1, &object));
}

class DataObjectTest : public ReadWriteSessionTest {
 public:
  DataObjectTest() : object_(CK_INVALID_HANDLE) {
    CK_OBJECT_CLASS data_class = CKO_DATA;
    CK_UTF8CHAR app[] = "pkcs11test";
    CK_UTF8CHAR label[] = "Label";
    CK_ATTRIBUTE attrs[] = {
      {CKA_CLASS, &data_class, sizeof(data_class)},
      {CKA_TOKEN, &g_ck_false, sizeof(g_ck_false)},  // Session object
      {CKA_APPLICATION, app, sizeof(app)},
      {CKA_VALUE, deadbeef, sizeof(deadbeef)},
      {CKA_LABEL, label, 5},
    };
    EXPECT_CKR_OK(g_fns->C_CreateObject(session_, attrs, sizeof(attrs)/sizeof(attrs[0]), &object_));
  }
  ~DataObjectTest() {
    if (object_ != CK_INVALID_HANDLE) {
      EXPECT_CKR_OK(g_fns->C_DestroyObject(session_, object_));
    }
  }
 protected:
    CK_OBJECT_HANDLE object_;
};

TEST_F(DataObjectTest, CopyDestroyObjectInvalid) {
  CK_ULONG object_size;
  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_GetObjectSize(INVALID_SESSION_HANDLE, object_, &object_size));
  EXPECT_CKR(CKR_OBJECT_HANDLE_INVALID,
             g_fns->C_GetObjectSize(session_, INVALID_OBJECT_HANDLE, &object_size));
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_GetObjectSize(session_, object_, NULL_PTR));

  CK_ATTRIBUTE attr;
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_CopyObject(session_, object_, &attr, 0, NULL_PTR));
  CK_OBJECT_HANDLE object2;
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_CopyObject(session_, object_, NULL, 1, &object2));
  CK_OBJECT_CLASS key_class = CKO_PUBLIC_KEY;
  CK_ATTRIBUTE attrs2[] = {
    {CKA_CLASS, &key_class, sizeof(key_class)},
  };
  EXPECT_CKR(CKR_ATTRIBUTE_READ_ONLY,
             g_fns->C_CopyObject(session_, object_, attrs2, 1, &object2));

  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID, g_fns->C_DestroyObject(INVALID_SESSION_HANDLE, object_));
  EXPECT_CKR(CKR_OBJECT_HANDLE_INVALID, g_fns->C_DestroyObject(session_, INVALID_OBJECT_HANDLE));
}

TEST_F(DataObjectTest, GetMultipleAttributes) {
  CK_BYTE buffer[128];
  CK_BYTE buffer2[128];
  CK_OBJECT_CLASS data_class;
  CK_BBOOL token;
  CK_ATTRIBUTE get_attrs[] = {
    {CKA_LABEL, buffer, sizeof(buffer)},
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {CKA_TOKEN, &token, sizeof(token)},
    {CKA_VALUE, buffer2, sizeof(buffer2)},
  };
  EXPECT_CKR_OK(g_fns->C_GetAttributeValue(session_, object_, get_attrs, sizeof(get_attrs) / sizeof(get_attrs[0])));
  EXPECT_EQ(CKO_DATA, data_class);
  EXPECT_EQ(CK_FALSE, token);
  EXPECT_EQ(5, get_attrs[0].ulValueLen);
  EXPECT_EQ(0, memcmp("Label", buffer, 5));
  EXPECT_EQ(4, get_attrs[3].ulValueLen);
  EXPECT_EQ(0, memcmp(deadbeef, buffer2, 4));
}

TEST_F(DataObjectTest, GetMultipleAttributesInvalid) {
  CK_BYTE buffer[128];
  CK_OBJECT_CLASS data_class;
  CK_ATTRIBUTE get_attrs[] = {
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {999, buffer, sizeof(buffer)},
  };
  EXPECT_CKR(CKR_ATTRIBUTE_TYPE_INVALID,
             g_fns->C_GetAttributeValue(session_, object_, get_attrs, sizeof(get_attrs) / sizeof(get_attrs[0])));
  // Even though the overall return code was failure, still expect the valid attribute to have a result.
  EXPECT_EQ(CKO_DATA, data_class);
  EXPECT_EQ((CK_ULONG)-1, get_attrs[1].ulValueLen);
}

TEST_F(DataObjectTest, GetSetAttributeInvalid) {
  CK_BYTE buffer[256];
  CK_ATTRIBUTE get_attr = {CKA_LABEL, buffer, sizeof(buffer)};
  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_GetAttributeValue(INVALID_SESSION_HANDLE, object_, &get_attr, 1));
  EXPECT_CKR(CKR_OBJECT_HANDLE_INVALID,
             g_fns->C_GetAttributeValue(session_, INVALID_OBJECT_HANDLE, &get_attr, 1));
  CK_RV rv = g_fns->C_GetAttributeValue(session_, object_, NULL_PTR, 1);
  EXPECT_TRUE(rv == CKR_ARGUMENTS_BAD || rv == CKR_TEMPLATE_INCOMPLETE) << " rv=" << CK_RV_(rv);
  get_attr.ulValueLen = 1;
  EXPECT_CKR(CKR_BUFFER_TOO_SMALL,
             g_fns->C_GetAttributeValue(session_, object_, &get_attr, 1));

  CK_UTF8CHAR new_label[] = "NewLabel";
  CK_ATTRIBUTE set_attr = {CKA_LABEL, new_label, 8};
  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_SetAttributeValue(INVALID_SESSION_HANDLE, object_, &set_attr, 1));
  EXPECT_CKR(CKR_OBJECT_HANDLE_INVALID,
             g_fns->C_SetAttributeValue(session_, INVALID_OBJECT_HANDLE, &set_attr, 1));

  CK_OBJECT_CLASS key_class = CKO_PUBLIC_KEY;
  CK_ATTRIBUTE set_attr_ro = {CKA_CLASS, &key_class, sizeof(key_class)};
  EXPECT_CKR(CKR_ATTRIBUTE_READ_ONLY,
             g_fns->C_SetAttributeValue(session_, object_, &set_attr_ro, 1));
}

TEST_F(DataObjectTest, FindObject) {
  CK_OBJECT_CLASS data_class = CKO_DATA;
  CK_UTF8CHAR app[] = "pkcs11test";
  CK_UTF8CHAR label[] = "Label";
  CK_ATTRIBUTE attrs[] = {
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {CKA_APPLICATION, app, sizeof(app)},
    {CKA_LABEL, label, 5},
  };
  EXPECT_CKR_OK(g_fns->C_FindObjectsInit(session_, attrs, 3));
  CK_OBJECT_HANDLE object[5];
  CK_ULONG count;
  EXPECT_CKR_OK(g_fns->C_FindObjects(session_, object, sizeof(object), &count));
  EXPECT_EQ(1, count);
  EXPECT_EQ(object_, object[0]);
  EXPECT_CKR_OK(g_fns->C_FindObjects(session_, object, sizeof(object), &count));
  EXPECT_EQ(0, count);
  EXPECT_CKR_OK(g_fns->C_FindObjectsFinal(session_));
}

TEST_F(DataObjectTest, FindNoObject) {
  CK_OBJECT_CLASS data_class = CKO_DATA;
  CK_UTF8CHAR app[] = "pkcs11test";
  CK_UTF8CHAR label[] = "LabelSuffix";
  CK_ATTRIBUTE attrs[] = {
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {CKA_APPLICATION, app, sizeof(app)},
    {CKA_LABEL, label, 11},
  };
  EXPECT_CKR_OK(g_fns->C_FindObjectsInit(session_, attrs, 3));
  CK_OBJECT_HANDLE object[5];
  CK_ULONG count;
  EXPECT_CKR_OK(g_fns->C_FindObjects(session_, object, sizeof(object), &count));
  EXPECT_EQ(0, count);
  EXPECT_CKR_OK(g_fns->C_FindObjectsFinal(session_));
}

TEST_F(DataObjectTest, FindObjectInvalid) {
  CK_OBJECT_CLASS data_class = CKO_DATA;
  CK_UTF8CHAR app[] = "pkcs11test";
  CK_UTF8CHAR label[] = "Label";
  CK_ATTRIBUTE attrs[] = {
    {CKA_CLASS, &data_class, sizeof(data_class)},
    {CKA_APPLICATION, app, sizeof(app)},
    {CKA_LABEL, label, 5},
  };

  // Find before initialization
  CK_OBJECT_HANDLE object[5];
  CK_ULONG count;
  EXPECT_CKR(CKR_OPERATION_NOT_INITIALIZED,
             g_fns->C_FindObjects(session_, object, sizeof(object), &count));

  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_FindObjectsInit(INVALID_SESSION_HANDLE, attrs, 3));
  /* TODO: reinstate when this doesn't trigger SEGV in OpenCryptoKi
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_FindObjectsInit(session_, NULL_PTR, 3));
  */
  EXPECT_CKR_OK(g_fns->C_FindObjectsInit(session_, attrs, 3));

  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_FindObjects(INVALID_SESSION_HANDLE, object, sizeof(object), &count));
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_FindObjects(session_, NULL_PTR, 1, &count));
  EXPECT_CKR(CKR_ARGUMENTS_BAD,
             g_fns->C_FindObjects(session_, object, sizeof(object), NULL_PTR));

  EXPECT_CKR(CKR_SESSION_HANDLE_INVALID,
             g_fns->C_FindObjectsFinal(INVALID_SESSION_HANDLE));
  EXPECT_CKR_OK(g_fns->C_FindObjectsFinal(session_));

  // Find after finalization
  EXPECT_CKR(CKR_OPERATION_NOT_INITIALIZED,
             g_fns->C_FindObjects(session_, object, sizeof(object), &count));
}

}  // namespace test
}  // namespace pkcs11
