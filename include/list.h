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
        Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

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

// Implementation

template <typename T>
List<T>::List() {}

template <typename T>
List<T>::List(std::initializer_list<T> init) {
    for (const T &x : init) {
        this->push_back(x);
    }
}

// copy
template <typename T>
List<T>::List(const List &list) {
    for (std::size_t i = 0; i < list.size(); i++) {
        this->push_back(list[i]);
    }
}

template <typename T>
List<T> &List<T>::operator=(const List &list) {
    this->clear();

    for (std::size_t i = 0; i < list.size(); i++) {
        this->push_back(list[i]);
    }

    return *this;
}

// move
template <typename T>
List<T>::List(List &&list)
    : size_(list.size_), head_(list.head_), tail_(list.tail_) {
    list.head_ = 0;
    list.tail_ = 0;
    list.size_ = 0;
}

template <typename T>
List<T> &List<T>::operator=(List &&list) {
    this->clear();

    size_ = list.size_;
    head_ = list.head_;
    tail_ = list.tail_;

    list.size_ = 0;
    list.head_ = nullptr;
    list.tail_ = nullptr;

    return *this;
}

// destructor
template <typename T>
List<T>::~List() {
    this->clear();
}

// unsafe
template <typename T>
ListNode<T> *List<T>::node_at(std::size_t index) const {
    ListNode<T> *node = head_;
    for (std::size_t i = 0; i < index; i++) {
        node = node->next;
    }
    return node;
}

template <typename T>
T &List<T>::operator[](std::size_t index) const {
    return this->node_at(index)->val;
}

// gets element at index, performing bounds checking
template <typename T>
T &List<T>::at(std::size_t index) const {
    if (index >= size_ - 1) {
        throw std::range_error("index out of range");
    }

    ListNode<T> *node = head_;
    for (std::size_t i = 0; i < index; i++) {
        node = node->next;
    }
    return node->val;   
}

template <typename T>
ListNode<T> *List<T>::push_back(T val) {
    ListNode<T> *node = new ListNode<T>(val);
    if (size_ == 0) {
        head_ = node;
        tail_ = node;
    } else {
        tail_->next = node;
        node->prev = tail_;
        tail_ = node;
    }
    size_++;
    return node;
}

template <typename T>
void List<T>::clear() {
    ListNode<T> *node = head_;
    ListNode<T> *next = nullptr;
    for (std::size_t i = 0; i < size_; i++) {
        next = node->next;
        delete node;
        node = next;
    }
    size_ = 0;
    head_ = nullptr;
    tail_ = nullptr;
}

template <typename T>
void List<T>::remove_node(ListNode<T> *node) {
    if (head_ == node) {
        head_ = node->next;
    } else {
        node->prev->next = node->next;
    }

    if (tail_ == node) {
        tail_ = node->prev;
    } else {
        node->next->prev = node->prev;
    }

    delete node;
    size_--;
}

} // namespace wolf

#endif // WOLF_QUEUE_H_INCLUDED
