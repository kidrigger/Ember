
#include "FreeList.hpp"

#include "HelperUtils.hpp"

FreeList::Iterator& FreeList::Iterator::operator++()
{
  Iter = Iter->Next;
  return *this;
}

bool FreeList::Iterator::operator==( Iterator const& other ) const
{
  return this->Iter == other.Iter;
}

FreeList::Node& FreeList::Iterator::operator*()
{
  return *Iter;
}

FreeList::FreeList() : m_Head{ .Next = &m_Tail, .Prev = nullptr }, m_Tail{ .Next = nullptr, .Prev = &m_Head }
{}

void FreeList::PushBack( Node* node )
{
  Node* prev = m_Tail.Prev;

  // Set prev as previous of node
  prev->Next = node;
  node->Prev = prev;

  // Set tail as next of node
  node->Next  = &m_Tail;
  m_Tail.Prev = node;
}

void FreeList::PushFront( Node* pNode )
{
  Node* next = m_Head.Next;

  // Set next as next of pNode
  next->Prev  = pNode;
  pNode->Next = next;

  // Set head as prev of pNode
  pNode->Prev = &m_Head;
  m_Head.Next = pNode;
}

FreeList::Node* FreeList::PopFront()
{
  ASSERT( not Empty() );

  Node* element       = m_Head.Next;
  element->Prev->Next = element->Next;
  element->Next->Prev = element->Prev;
  return element;
}

void FreeList::Clear()
{
  m_Head.Next = &m_Tail;
  m_Tail.Prev = &m_Head;
}

bool FreeList::Empty() const
{
  return m_Head.Next == &m_Tail;
}

FreeList::Iterator FreeList::begin()
{
  return { m_Head.Next };
}

FreeList::Iterator FreeList::end()
{
  return { &m_Tail };
}

FreeList::FreeList( FreeList&& other ) noexcept : m_Head{ other.m_Head }, m_Tail{ other.m_Tail }
{
  m_Head.Next->Prev = &m_Head;
  m_Tail.Prev->Next = &m_Tail;

  other.Clear();
}

FreeList& FreeList::operator=( FreeList&& other ) noexcept
{
  if ( &other == this ) return *this;

  if ( other.Empty() )
  {
    // Empty list movement doesn't work since these nodes are moved.
    m_Head.Next = &m_Tail;
    m_Head.Prev = nullptr;
    m_Tail.Next = nullptr;
    m_Tail.Prev = &m_Head;
  }

  // Next->Prev and Prev->Next are actual elements.
  m_Head            = other.m_Head;
  m_Tail            = other.m_Tail;
  m_Head.Next->Prev = &m_Head;
  m_Tail.Prev->Next = &m_Tail;

  other.Clear();

  return *this;
}
