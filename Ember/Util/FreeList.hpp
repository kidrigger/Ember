#pragma once

struct FreeList
{
  struct Node
  {
    Node* Next;
    Node* Prev;
  };

  struct Iterator
  {
    Node*     Iter;

    Iterator& operator++();
    bool      operator==( Iterator const& other ) const;
    Node&     operator*();
  };

private:
  Node m_Head;
  Node m_Tail;

public:
  FreeList();

  void               PushBack( Node* node );
  void               PushFront( Node* node );
  Node*              PopFront();
  [[nodiscard]] bool Empty() const;

  Iterator           begin();
  Iterator           end();

  FreeList( FreeList&& ) noexcept;
  FreeList& operator=( FreeList&& ) noexcept;

  FreeList( FreeList const& )            = delete;
  FreeList& operator=( FreeList const& ) = delete;

  ~FreeList()                            = default;
};
