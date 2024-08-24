#ifndef WOLF_QUEUE_H_INCLUDED
#define WOLF_QUEUE_H_INCLUDED

#include <cstddef>
#include <initializer_list>
#include <iterator>

namespace wolf {

template <typename T>
class ListNode {
public:
    ListNode(T v) : val(v) {}
    ListNode(T v, ListNode<T> *n, ListNode<T> *p) : val(v), prev(p), next(n) {}

    T val;
    ListNode<T> *prev = nullptr;
    ListNode<T> *next = nullptr;
};

template <typename T>
class List {
public:
    List();
    List(std::initializer_list<T> init);

    List(const List &list);
    List &operator=(const List &list);
    List(List &&list);
    List &operator=(List &&list);

    ~List();

    ListNode<T> *node_at(std::size_t index) const;
    T &operator[](std::size_t index) const;
    T &at(std::size_t index) const;

    ListNode<T> *push_back(T val);

    void clear();
    void remove_node(ListNode<T> *node);


    std::size_t size() const { return size_; };

    struct Iterator {
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = T;
        using pointer           = ListNode<T>*;
        using reference         = T&;

        Iterator(pointer ptr) : ptr_(ptr) {}

        reference operator*() const { return ptr_->val; }
        pointer operator->() { return ptr_; };

        Iterator& operator++() { ptr_ = ptr_->next; return *this; }
        Iterator operator++(T) { Iterator tmp = *this; ++(*this); return tmp; }

        friend bool operator== (const Iterator& a, const Iterator& b) { return a.ptr_ == b.ptr_; };
        friend bool operator!= (const Iterator& a, const Iterator& b) { return a.ptr_ != b.ptr_; }; 

    private:
        pointer ptr_;
    };

    Iterator begin() const { return Iterator(head_); }
    Iterator end() const { return Iterator(nullptr); }

private:
    ListNode<T> *head_ = nullptr;
    ListNode<T> *tail_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace wolf

#endif // WOLF_QUEUE_H_INCLUDED
