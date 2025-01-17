/** @file

    Trie implementation for 8-bit string keys.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tscore/List.h"
#include "tscore/Diags.h"

class TrieImpl
{
protected:
  inline static DbgCtl dbg_ctl_insert{"Trie::Insert"};
  inline static DbgCtl dbg_ctl_search{"Trie::Search"};
};

// Note that you should provide the class to use here, but we'll store
// pointers to such objects internally.
template <typename T> class Trie : private TrieImpl
{
public:
  Trie() { m_root.Clear(); }
  // will return false for duplicates; key should be nullptr-terminated
  // if key_len is defaulted to -1
  bool Insert(const char *key, T *value, int rank, int key_len = -1);

  // will return false if not found; else value_ptr will point to found value
  T *Search(const char *key, int key_len = -1) const;
  void Clear();
  void Print() const;

  bool
  Empty() const
  {
    return m_value_list.empty();
  }

  virtual ~Trie() { Clear(); }

  using const_iterator = typename Queue<T>::const_iterator;
  const_iterator
  begin() const
  {
    return m_value_list.begin();
  }
  const_iterator
  end() const
  {
    return m_value_list.end();
  }

private:
  static const int N_NODE_CHILDREN = 256;

  class Node
  {
  public:
    T *value;
    bool occupied;
    int rank;

    void
    Clear()
    {
      value    = nullptr;
      occupied = false;
      rank     = 0;
      ink_zero(children);
    }

    void Print(const DbgCtl &dbg_ctl) const;
    inline Node *
    GetChild(char index) const
    {
      return children[static_cast<unsigned char>(index)];
    }
    inline Node *
    AllocateChild(char index)
    {
      Node *&child = children[static_cast<unsigned char>(index)];
      ink_assert(child == nullptr);
      child = static_cast<Node *>(ats_malloc(sizeof(Node)));
      child->Clear();
      return child;
    }

  private:
    Node *children[N_NODE_CHILDREN];
  };

  Node m_root;
  Queue<T> m_value_list;

  void _CheckArgs(const char *key, int &key_len) const;
  void _Clear(Node *node);

  // make copy-constructor and assignment operator private
  // till we properly implement them
  Trie(const Trie<T> &rhs){};
  Trie &
  operator=(const Trie<T> &rhs)
  {
    return *this;
  }
};

template <typename T>
void
Trie<T>::_CheckArgs(const char *key, int &key_len) const
{
  if (!key) {
    key_len = 0;
  } else if (key_len == -1) {
    key_len = strlen(key);
  }
}

template <typename T>
bool
Trie<T>::Insert(const char *key, T *value, int rank, int key_len /* = -1 */)
{
  _CheckArgs(key, key_len);

  Node *next_node;
  Node *curr_node = &m_root;
  int i           = 0;

  while (true) {
    if (is_dbg_ctl_enabled(dbg_ctl_insert)) {
      Dbg(dbg_ctl_insert, "Visiting Node...");
      curr_node->Print(dbg_ctl_insert);
    }

    if (i == key_len) {
      break;
    }

    next_node = curr_node->GetChild(key[i]);
    if (!next_node) {
      while (i < key_len) {
        Dbg(dbg_ctl_insert, "Creating child node for char %c (%d)", key[i], key[i]);
        curr_node = curr_node->AllocateChild(key[i]);
        ++i;
      }
      break;
    }
    curr_node = next_node;
    ++i;
  }

  if (curr_node->occupied) {
    Dbg(dbg_ctl_insert, "Cannot insert duplicate!");
    return false;
  }

  curr_node->occupied = true;
  curr_node->value    = value;
  curr_node->rank     = rank;
  m_value_list.enqueue(curr_node->value);
  Dbg(dbg_ctl_insert, "inserted new element!");
  return true;
}

template <typename T>
T *
Trie<T>::Search(const char *key, int key_len /* = -1 */) const
{
  _CheckArgs(key, key_len);

  const Node *found_node = nullptr;
  const Node *curr_node  = &m_root;
  int i                  = 0;

  while (curr_node) {
    if (is_dbg_ctl_enabled(dbg_ctl_search)) {
      DbgPrint(dbg_ctl_search, "Visiting node...");
      curr_node->Print(dbg_ctl_search);
    }
    if (curr_node->occupied) {
      if (!found_node || curr_node->rank <= found_node->rank) {
        found_node = curr_node;
      }
    }
    if (i == key_len) {
      break;
    }
    curr_node = curr_node->GetChild(key[i]);
    ++i;
  }

  if (found_node) {
    Dbg(dbg_ctl_search, "Returning element with rank %d", found_node->rank);
    return found_node->value;
  }

  return nullptr;
}

template <typename T>
void
Trie<T>::_Clear(Node *node)
{
  Node *child;

  for (int i = 0; i < N_NODE_CHILDREN; ++i) {
    child = node->GetChild(i);
    if (child) {
      _Clear(child);
      ats_free(child);
    }
  }
}

template <typename T>
void
Trie<T>::Clear()
{
  T *iter;
  while (nullptr != (iter = m_value_list.pop())) {
    delete iter;
  }

  _Clear(&m_root);
  m_root.Clear();
}

template <typename T>
void
Trie<T>::Print() const
{
  // The class we contain must provide a ::Print() method.
  forl_LL(T, iter, m_value_list) iter->Print();
}

template <typename T>
void
Trie<T>::Node::Print(const DbgCtl &dbg_ctl) const
{
  if (occupied) {
    Dbg(dbg_ctl, "Node is occupied");
    Dbg(dbg_ctl, "Node has rank %d", rank);
  } else {
    Dbg(dbg_ctl, "Node is not occupied");
  }

  for (int i = 0; i < N_NODE_CHILDREN; ++i) {
    if (GetChild(i)) {
      Dbg(dbg_ctl, "Node has child for char %c", static_cast<char>(i));
    }
  }
}
