// nx_array - реализация динамического массива, гарантирующего стабильность указателей и доступ через []
// Версия модуля: 2026-11-11 release / Qt-версия (C++11)
#ifndef NX_ARRAY_H
#define NX_ARRAY_H

#include <list>
#include <array>
#include <string>
#include <cassert>
#include <iterator>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <initializer_list>
#include <QString>
#include <QList>
#include <vector>
#include <deque>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_set>

static_assert(__cplusplus >= 201103L, "This header requires C++11 or later");

namespace nx_array_detail { struct iterator_tag {}; }

template <typename T, typename Allocator = std::allocator<T>>
class nx_array
{   
   template<class, class> friend class nx_array;
   template<class U>
   struct can_delete : std::integral_constant<bool, std::is_pointer<U>::value &&
      std::is_object<typename std::remove_pointer<U>::type>::value> {};

   using list_type = std::list<T, Allocator>;

public:

   // алиасы для STL-совместимости
   using size_type         = typename std::allocator<T>::size_type;
   using iterator_category = std::random_access_iterator_tag;
   using value_type        = T;
   using difference_type   = std::ptrdiff_t;
   using pointer           = T*;  // Указатель на константу
   using reference         = T&;  // Ссылка на константу
   using allocator_type    = Allocator;
   using const_reference   = const T&;
   using const_pointer     = const T*;

private:

   list_type data_list;                // основное хранилище
   std::vector<T*> ptr_vector;         // указатели для доступа по индексу[]
   size_type reserved_capacity = 0;    // зарезервированные элементы

   enum RebuildMode
   {
        REBUILD_FULL,
        REBUILD_APPEND,
        REBUILD_PREPEND,
        REBUILD_INSERT,
        REBUILD_REMOVE,
        REBUILD_MOVE,
        REBUILD_SWAP
   };

//===================================================================================================================================================
//===================================================================================================================================================

   // ФУНКЦИИ НЕПРЯМОГО ВЫЗОВА МЕТОДОВ ptr_vector и data_list
   // internal_xxx()   -- методы std::vector или им подобные для работы с ptr_vector
   // internal_L_xxx() -- методы для работы с data_list (могут менять и ptr_vector тоже)
   // internal_S_xxx() -- вспомогательные методы для других internal_

//-------- ОСНОВНЫЕ ОПЕРАЦИИ

   // -- reserve()
   void internal_reserve(size_type new_capacity)
   {
      ptr_vector.reserve(new_capacity);
      reserved_capacity = ptr_vector.capacity();
   }
//--

   // -- shrink_to_fit()
   void internal_shrink_to_fit(size_type min_capacity = 0)
   {
      if(ptr_vector.capacity() > 2 * std::max(reserved_capacity, ptr_vector.size()))
      {
         ptr_vector.shrink_to_fit();
         reserved_capacity = std::max(ptr_vector.capacity(), min_capacity);
      }
   }
//--

   // -- resize()
   void internal_resize(size_type new_size, T* fill_value = nullptr)
   {
      ptr_vector.resize(new_size, fill_value);
      reserved_capacity = std::max(reserved_capacity, ptr_vector.capacity());
   }
//--

   // -- this->clear()
   void internal_clear()
   {
      ptr_vector.clear();
      reserved_capacity = ptr_vector.capacity();
   }
//--

//-------- МОДИФИЦИРУЮЩИЕ ОПЕРАЦИИ

   // -- assign()
   template <class InputIt>
   void internal_assign(InputIt first, InputIt last)
   {
      ptr_vector.assign(first, last);
      reserved_capacity = ptr_vector.capacity();
   }
//--

   //
   void internal_push_front(T* ptr)
   {
      if(ptr_vector.size() == reserved_capacity) internal_reserve(std::max(reserved_capacity * 2, size_type(1)));
      ptr_vector.insert(ptr_vector.begin(), ptr);
   }
//--

   // -- push_back()
   void internal_push_back(T* ptr)
   {
      if(ptr_vector.size() == reserved_capacity) internal_reserve(std::max(reserved_capacity * 2, size_type(1)));
      ptr_vector.push_back(ptr);
   }
//--

   // -- insert() //single
   void internal_insert(size_type pos, T* ptr)
   {
      if(ptr_vector.size() == reserved_capacity) internal_reserve(std::max(reserved_capacity * 2, size_type(1)));
      ptr_vector.insert(ptr_vector.begin() + pos, ptr);
   }
//--

   // -- insert() //range по индексу и количеству
   void internal_insert_range(size_type pos, size_type count, T* ptr)
   {
      if(ptr_vector.size() + count > reserved_capacity) internal_reserve(std::max(reserved_capacity * 2, ptr_vector.size() + count));
      ptr_vector.insert(ptr_vector.begin() + pos, count, ptr);
   }
//--

   // -- insert() //range по итераторам
   template <class InputIt>
   void internal_insert_range(size_type pos, InputIt first, InputIt last)
   {
      size_type count = std::distance(first, last);
      if(ptr_vector.size() + count > reserved_capacity) internal_reserve(std::max(reserved_capacity * 2, ptr_vector.size() + count));
      ptr_vector.insert(ptr_vector.begin() + pos, first, last);
   }
//--

   // -- erase() до позиции index
   void internal_erase(size_type pos)
   {
      ptr_vector.erase(ptr_vector.begin() + pos);
      internal_S_try_shrink();                              // автоматическое сжатие при необходимости
   }
//--

   // -- erase() между итераторами
   void internal_erase_range(size_type first, size_type last)
   {
      ptr_vector.erase(ptr_vector.begin() + first, ptr_vector.begin() + last);
      internal_S_try_shrink();
   }
//--

   // Специальная функция для замены data_list.splice

   //версия для индексов
   void internal_L_splice(size_type pos, list_type&& other_list)
   {
      if(&other_list == &data_list) throw std::invalid_argument("NxArray: cannot splice list into itself");
      if(pos > size()) throw std::out_of_range("NxArray: invalid splice position");
      if(data_list.get_allocator() != other_list.get_allocator())
         throw std::invalid_argument("NxArray: splice requires equal allocators");
      if(other_list.size() > max_size() - size()) throw std::length_error("NxArray: splice exceeds max_size()");
      if(other_list.empty()) return;
      internal_reserve(std::max(capacity(), size() + other_list.size()));
      auto dest = data_list.begin();
      std::advance(dest, pos);
      data_list.splice(dest, other_list);
      rebuild_pointers();
   }

   // версия для итераторов
   void internal_L_splice(size_type pos, typename list_type::iterator item, list_type& source)
   {
      if(pos > size()) throw std::out_of_range("NxArray: invalid splice position");
      if(data_list.get_allocator() != source.get_allocator())
         throw std::invalid_argument("NxArray: splice requires equal allocators");
      if(item == source.end()) throw std::out_of_range("NxArray: cannot splice end iterator");
      if(&source != &data_list)
      {
         if(size() == max_size()) throw std::length_error("NxArray: splice exceeds max_size()");
         internal_reserve(std::max(capacity(), size() + 1));
      }
      auto dest = data_list.begin();
      std::advance(dest, pos);
      data_list.splice(dest, source, item);
      rebuild_pointers();
   }

//-------- вспомогательные операции

   void internal_S_try_shrink()
   {
      if(ptr_vector.capacity() > 2 * std::max(reserved_capacity, ptr_vector.size())) internal_shrink_to_fit(reserved_capacity);
   }
//--

   void internal_S_validate_capacity() const
   {
      assert(ptr_vector.size() <= reserved_capacity);
      assert(ptr_vector.capacity() == reserved_capacity);
   }
//--

   size_type internal_S_calculate_new_capacity(size_type added) const
   {
      return(std::max(reserved_capacity * 2, ptr_vector.size() + added));
   }


//===================================================================================================================================================
//===================================================================================================================================================

   void rebuild_pointers(RebuildMode mode = REBUILD_FULL, size_type position = size_type(-1), size_type count = 0)
   {
      if(mode == REBUILD_APPEND && data_list.size() == 1)
      {
         internal_push_back(std::addressof(data_list.back()));
         return;
      }

      switch(mode)
      {
         case REBUILD_APPEND:
         {
            size_type old_size = ptr_vector.size();
            size_type new_elements = data_list.size() - old_size;

            if(new_elements == 0) break; // ничего не добавляем

            // проверяем, хватит ли места для всех новых элементов
            if(ptr_vector.size() + new_elements > reserved_capacity)
            {
               rebuild_pointers(REBUILD_FULL);
               break;
            }

            // эффективно добавляем новые элементы за один проход
            auto it = data_list.begin();
            std::advance(it, old_size); // переходим к первому новому элементу

            for(; it != data_list.end(); it++)
            {
               internal_push_back(std::addressof(*it));
            }
            break;
         }

         case REBUILD_PREPEND:
            if(!data_list.empty()) ptr_vector.insert(ptr_vector.begin(), std::addressof(data_list.front()));
            break;

         case REBUILD_INSERT:
         {
            if(position >= ptr_vector.size())
            {
               rebuild_pointers(REBUILD_FULL);
               return;
            }

            // динамический расчет max_insert_count
            size_type max_insert_count;
            if(data_list.size() <= 100) max_insert_count = 10;       // для малых списков
            else if(data_list.size() <= 10000) max_insert_count = 5; // для средних списков
            else max_insert_count = 3;                                // для крупных списков
            if(sizeof(T) > 64) max_insert_count /= 2;
            // если вставка во второй половине списка, уменьшаем max_insert_count
            if(static_cast<size_type>(position) > data_list.size() / 2) max_insert_count = std::max(1, int(max_insert_count) - 2);

            if(count > max_insert_count || ptr_vector.size() + count > reserved_capacity)
            {
               rebuild_pointers(REBUILD_FULL);
               return;
            }

            // вставка элементов
            auto it = data_list.begin();
            std::advance(it, position);
            for(size_type i = 0; i < count && it != data_list.end(); i++, it++)
            {
               ptr_vector.insert(ptr_vector.begin() + position + i, std::addressof(*it));
            }
            break;
         }

         case REBUILD_REMOVE:
         {
            if(position >= ptr_vector.size())
            {
               rebuild_pointers(REBUILD_FULL);
               return;
            }

            ptr_vector.erase(ptr_vector.begin() + position, ptr_vector.begin() + position + count);
            break;
         }

         case REBUILD_MOVE:
         {
            if(ptr_vector.size() != data_list.size()) rebuild_pointers(REBUILD_FULL);
            break;
         }

         case REBUILD_SWAP: break;

         case REBUILD_FULL:
         default:
            ptr_vector.clear();
            if(reserved_capacity < data_list.size()) reserved_capacity = data_list.size();

            internal_reserve(reserved_capacity);
            for(auto& item : data_list)
            {
               ptr_vector.push_back(std::addressof(item));
            }
            break;
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template <typename InputIt>
   static void check_range(InputIt, InputIt, std::input_iterator_tag) {}

   template <typename RandomIt>
   static void check_range(RandomIt first, RandomIt last, std::random_access_iterator_tag)
   {
      if(first != last && last - first < 0)
         throw std::invalid_argument("NxArray: reversed iterator range");
   }

   template <typename InputIt>
   void check_source(InputIt, InputIt, std::false_type) const {}

   template <typename InputIt>
   void check_source(InputIt first, InputIt last, std::true_type) const
   {
      if(first == last) return;
      if(!first.container() || first.container() != last.container())
         throw std::invalid_argument("NxArray: invalid source range");
      first.container()->checkIterator(first);
      first.container()->checkIterator(last);
      check_copy_ownership(*first.container());
   }

   template <typename InputIt>
   void check_range_source(InputIt first, InputIt last) const
   {
      check_source(first, last, typename std::is_base_of<nx_array_detail::iterator_tag, InputIt>::type());
   }

   template <typename It>
   void check_range_source(std::move_iterator<It> first, std::move_iterator<It> last) const
   {
      check_range_source(first.base(), last.base());
   }

   template <typename It>
   void check_range_source(std::reverse_iterator<It> first, std::reverse_iterator<It> last) const
   {
      check_range_source(last.base(), first.base());
   }

   template <typename InputIt>
   void stage_range(list_type& pending, InputIt first, InputIt last, size_type limit)
   {
      check_range_source(first, last);
      check_range(first, last, typename std::iterator_traits<InputIt>::iterator_category());
      for(; first != last; ++first)
      {
         if(pending.size() == limit) throw std::length_error("NxArray: range exceeds max_size()");
         pending.emplace_back(*first);
      }
   }

   template <typename InputIt>
   void stage_selected(list_type& pending, InputIt first, InputIt last, size_type limit, std::true_type)
   {
      stage_range(pending, first, last, limit);
   }

   template <typename InputIt>
   void stage_selected(list_type&, InputIt, InputIt, size_type, std::false_type)
   {
      throw std::invalid_argument("NxArray: T cannot be constructed from this range in the selected copy/move mode");
   }

   template <typename Container>
   static auto clear_source(Container& cont, int) -> decltype(cont.clear(), void())
   {
      cont.clear();
   }

   template <typename Container>
   static void clear_source(Container&, long) {} // Fixed-size containers have no clear().

   void insert_pending(size_type index, list_type& pending)
   {
      if(index > size()) throw std::out_of_range("NxArray: invalid insertion index");
      if(pending.size() > max_size() - size()) throw std::length_error("NxArray: range exceeds max_size()");
      if(pending.empty()) return;
      internal_reserve(std::max(capacity(), size() + pending.size()));
      auto pos = data_list.begin();
      std::advance(pos, index);
      data_list.splice(pos, pending);
      rebuild_pointers(); // All pointer capacity was reserved before the splice.
   }

   void replace_pending(list_type& pending)
   {
      if(pending.size() > max_size()) throw std::length_error("NxArray: replacement exceeds max_size()");
      std::vector<T*> new_index;
      new_index.reserve(std::max(capacity(), pending.size()));
      for(auto& item : pending) new_index.push_back(std::addressof(item));
      data_list.swap(pending);
      ptr_vector.swap(new_index);
      reserved_capacity = ptr_vector.capacity();
   }

   template <typename InputIt>
   void insert_elements(InputIt first, InputIt last, int position, bool useExc = false)
   {
      if(position < END_POS || (position > 0 && static_cast<size_type>(position) > size()))
      {
         if(useExc) throw std::out_of_range("NxArray: invalid insertion position");
         position = END_POS;
      }
      size_type index = position == END_POS ? size() : static_cast<size_type>(position);
      list_type pending(data_list.get_allocator());
      stage_range(pending, first, last, max_size() - size());
      insert_pending(index, pending);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // index is a validated insertion position, including end().
   template<class... Args>
   T& emplace_at(size_type index, Args&&... args)
   {
      if(size() >= max_size()) throw std::length_error("NxArray: insertion exceeds max_size()");
      if(size() == ptr_vector.capacity())
         internal_reserve(std::min(max_size(), internal_S_calculate_new_capacity(1)));
      auto pos = data_list.end();
      if(index != size())
      {
         pos = data_list.begin();
         std::advance(pos, index);
      }
      auto node = data_list.emplace(pos, std::forward<Args>(args)...);
      internal_insert(index, std::addressof(*node));
      return *node;
   }

   int qlist_capacity(size_type requested) const
   {
      const size_type n = (std::max)(requested, size());
      if(n > static_cast<size_type>((std::numeric_limits<int>::max)()))
         throw std::length_error("NxArray: size exceeds QList capacity");
      return static_cast<int>(n);
   }

   template<class U, class A>
   void check_copy_ownership(const nx_array<U,A>& other) const
   {
      if(static_cast<const void*>(this) != static_cast<const void*>(&other) &&
         can_delete<T>::value && isDeleteItemsOnDestroy &&
         other.isDeleteItemsOnDestroy && !other.empty())
         throw std::invalid_argument("NxArray: cannot share pointees between owners");
   }

   template<class U, class A>
   void stage_transfer(list_type& pending, nx_array<U,A>& source, size_type limit)
   {
      if(source.empty()) return;
      if(source.size() > limit) throw std::length_error("NxArray: transfer exceeds max_size()");
      if(can_delete<U>::value && source.isDeleteItemsOnDestroy &&
         !std::is_same<T,U>::value)
         throw std::invalid_argument("NxArray: cannot change the element type of owned pointers");
      auto first = std::make_move_iterator(source.data_list.begin());
      auto last = std::make_move_iterator(source.data_list.end());
      stage_selected(pending, first, last, limit, typename std::is_constructible<T,U&&>::type());
   }

   // Replacement inherits ownership only after both the nodes and the index are ready.
   template<class U, class A>
   void move_values(nx_array<U,A>& source)
   {
      list_type pending(data_list.get_allocator());
      stage_transfer(pending, source, max_size());
      replace_pending(pending);
      isDeleteItemsOnDestroy = source.isDeleteItemsOnDestroy;
      source.clear();
      source.isDeleteItemsOnDestroy = false;
   }

   void move_storage(nx_array& source, std::true_type)
   {
      internal_reserve(std::max(size(), source.size()));
      data_list = std::move(source.data_list);
      rebuild_pointers();
   }

   void move_storage(nx_array& source, std::false_type)
   {
      if(source.size() > max_size()) throw std::length_error("NxArray: move exceeds max_size()");
      if(data_list.get_allocator() == source.data_list.get_allocator())
      {
         internal_reserve(std::max(capacity(), source.size()));
         data_list.clear();
         data_list.splice(data_list.end(), source.data_list);
         rebuild_pointers();
      }
      else
      {
         list_type pending(data_list.get_allocator());
         stage_transfer(pending, source, max_size());
         replace_pending(pending);
      }
   }

   template<class U, class A>
   void append_transfer(nx_array<U,A>& source, size_type index)
   {
      if(index > size()) throw std::out_of_range("NxArray: invalid insertion position");
      if(source.empty()) return;
      if(can_delete<T>::value && isDeleteItemsOnDestroy != source.isDeleteItemsOnDestroy)
         throw std::invalid_argument("NxArray: insertion requires matching ownership modes");
      list_type pending(data_list.get_allocator());
      stage_transfer(pending, source, max_size() - size());
      insert_pending(index, pending);
      source.clear();
   }

   template<class U, class A>
   void check_temporary(const nx_array<U,A>& source) const
   {
      if(std::is_pointer<T>::value && can_delete<U>::value && source.isDeleteItemsOnDestroy && !source.empty())
         throw std::invalid_argument("NxArray: cannot borrow pointers from a temporary owner");
   }

   template<class U, class A>
   void copy_temporary(const nx_array<U,A>& source)
   {
      check_temporary(source);
      list_type pending(data_list.get_allocator());
      stage_selected(pending, source.begin(), source.end(), max_size(),
                     typename std::is_constructible<T,const U&>::type());
      replace_pending(pending);
   }

   void transfer_nodes(nx_array& source, size_type index)
   {
      if(this == &source) throw std::invalid_argument("NxArray: cannot splice array into itself");
      if(source.empty()) return;
      if(can_delete<T>::value && isDeleteItemsOnDestroy != source.isDeleteItemsOnDestroy)
         throw std::invalid_argument("NxArray: splice requires matching ownership modes");
      internal_L_splice(index, std::move(source.data_list));
      source.internal_clear();
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

   void check_index(size_t index, unsigned int msg_num = 0) const
   {
      if(index >= ptr_vector.size())
      {
         if(msg_num == 0) throw std::out_of_range("NxArray: index out of range");
         else if(msg_num == 1) throw std::out_of_range("NxArray: start index out of range");
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // проверка инвариантов
   void validate_invariants() const
   {
      assert(validate_pointers() && "NxArray: inconsistent pointer index");
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------







//===================================================================================================================================================
//========= PUBLIC ==================================================================================================================================
//===================================================================================================================================================






public:

   // Assignment replaces raw pointers without deleting their pointees.
   // Use clear_with_delete() explicitly before replacement when disposal is intended.
   // Copies are shallow. Copy construction produces a non-owning view.
   // Transferring nodes between pointer arrays requires matching ownership modes.
   // Never give two owners the same object through raw pointers or non-owning views.
   bool isDeleteItemsOnDestroy = false;   // удаление объектов-элементов массива в деструкторе

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // деструктор
   ~nx_array()
   {
      if(isDeleteItemsOnDestroy && !data_list.empty() && std::is_pointer<T>::value) clear_with_delete();
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Iterators identify positions. Insertions, erasures and reordering can change
   // which node an iterator denotes, even when the node address stays valid.
   class random_access_iterator : public nx_array_detail::iterator_tag
   {
      nx_array* m_container;
      size_type m_index;

      friend class const_random_access_iterator;

   public:

      using iterator_category = std::random_access_iterator_tag;
      using value_type        = typename nx_array::value_type;
      using difference_type   = typename nx_array::difference_type;
      using pointer           = typename nx_array::pointer;
      using reference         = typename nx_array::reference;
      using const_pointer     = typename nx_array::const_pointer;
      using const_reference   = typename nx_array::const_reference;


      random_access_iterator(nx_array* container = nullptr, size_type index = 0) : m_container(container), m_index(index) {}

      nx_array* container() const { return m_container; }
      size_type index() const { return m_index; }

      reference operator[](difference_type n) const { return *(*this + n); }

      reference operator*() { return (*m_container)[m_index]; }

      reference operator*() const { return (*m_container)[m_index]; }

      pointer operator->() const { return std::addressof((*m_container)[m_index]); }

      pointer operator->() { return std::addressof((*m_container)[m_index]); }

      random_access_iterator& operator++()
      {
         m_index++;
         return *this;
      }

      random_access_iterator operator++(int)
      {
         random_access_iterator tmp = *this;
         m_index++;
         return tmp;
      }

      random_access_iterator& operator--()
      {
         m_index--;
         return *this;
      }

      random_access_iterator operator--(int)
      {
         random_access_iterator tmp = *this;
         m_index--;
         return tmp;
      }

      // Арифметические операторы
      random_access_iterator operator+(difference_type n) const
      {
         return random_access_iterator(m_container, m_index + n);
      }

      random_access_iterator operator-(difference_type n) const
      {
         return random_access_iterator(m_container, m_index - n);
      }

      difference_type operator-(const random_access_iterator& other) const
      {
         assert(m_container == other.m_container);
         return static_cast<difference_type>(m_index) - static_cast<difference_type>(other.m_index);
      }

      // Сравнение
      random_access_iterator& operator+=(difference_type n) { m_index += n; return *this; }
      random_access_iterator& operator-=(difference_type n) { m_index -= n; return *this; }
      bool operator>(const random_access_iterator& other) const { return other < *this; }
      bool operator<=(const random_access_iterator& other) const { return !(other < *this); }
      bool operator>=(const random_access_iterator& other) const { return !(*this < other); }

      bool operator==(const random_access_iterator& other) const
      {
         return m_container == other.m_container && m_index == other.m_index;
      }

      bool operator!=(const random_access_iterator& other) const
      {
         return !(*this == other);
      }

      bool operator<(const random_access_iterator& other) const
      {
         return m_index < other.m_index;
      }
   };

   class const_random_access_iterator : public nx_array_detail::iterator_tag
   {
      const nx_array* m_container;  // Указатель на константный контейнер
      size_type m_index;

   public:

      using iterator_category = std::random_access_iterator_tag;
      using value_type        = typename nx_array::value_type;
      using difference_type   = typename nx_array::difference_type;
      using pointer           = typename nx_array::const_pointer;
      using reference         = typename nx_array::const_reference;

      const_random_access_iterator(const nx_array* container = nullptr, size_type index = 0) : m_container(container), m_index(index) {}

      const_random_access_iterator(const random_access_iterator& other) : m_container(other.container()), m_index(other.index()) {}

      reference operator[](difference_type n) const { return *(*this + n); }

      reference operator*() const { return (*m_container)[m_index]; }

      pointer operator->() const { return std::addressof((*m_container)[m_index]); }

      const_random_access_iterator& operator++()
      {
         m_index++;
         return *this;
      }

      const_random_access_iterator operator++(int)
      {
         const_random_access_iterator tmp = *this;
         m_index++;
         return tmp;
      }

      const_random_access_iterator& operator--()
      {
          m_index--;
          return *this;
      }

      const_random_access_iterator operator--(int)
      {
         const_random_access_iterator tmp = *this;
         m_index--;
         return tmp;
      }

      // Арифметические операторы
      const_random_access_iterator operator+(difference_type n) const
      {
         return const_random_access_iterator(m_container, m_index + n);
      }

      const_random_access_iterator operator-(difference_type n) const
      {
         return const_random_access_iterator(m_container, m_index - n);
      }

      difference_type operator-(const const_random_access_iterator& other) const
      {
         assert(m_container == other.m_container);
         return static_cast<difference_type>(m_index) - static_cast<difference_type>(other.m_index);
      }

      // Сравнение
      const_random_access_iterator& operator+=(difference_type n) { m_index += n; return *this; }
      const_random_access_iterator& operator-=(difference_type n) { m_index -= n; return *this; }
      friend const_random_access_iterator operator+(difference_type n, const_random_access_iterator it) { return it += n; }
      bool operator>(const const_random_access_iterator& other) const { return other < *this; }
      bool operator<=(const const_random_access_iterator& other) const { return !(other < *this); }
      bool operator>=(const const_random_access_iterator& other) const { return !(*this < other); }

      bool operator==(const const_random_access_iterator& other) const
      {
         return m_container == other.m_container && m_index == other.m_index;
      }

      bool operator!=(const const_random_access_iterator& other) const
      {
         return !(*this == other);
      }

      bool operator<(const const_random_access_iterator& other) const
      {
         return m_index < other.m_index;
      }

      friend bool operator==(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { return lhs.container() == rhs.m_container && lhs.index() == rhs.m_index; }
      friend bool operator!=(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { return !(lhs == rhs); }
      friend bool operator<(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { assert(lhs.container() == rhs.m_container); return lhs.index() < rhs.m_index; }
      friend bool operator>(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { return rhs < lhs; }
      friend bool operator<=(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { return !(rhs < lhs); }
      friend bool operator>=(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { return !(lhs < rhs); }
      friend difference_type operator-(const random_access_iterator& lhs, const const_random_access_iterator& rhs)
      { return const_random_access_iterator(lhs) - rhs; }

      const nx_array* container() const { return m_container; }
      size_type index() const { return m_index; }
   };
//===================================================================================================================================================




//===================================================================================================================================================
//===================================================================================================================================================

   // Константы для позиционирования
   static const int START_POS = 0;
   static const int END_POS = -1;

   using iterator = random_access_iterator;
   using const_iterator = const_random_access_iterator;
   using reverse_iterator = std::reverse_iterator<iterator>;
   using const_reverse_iterator = std::reverse_iterator<const_iterator>;

//===================================================================================================================================================
//===================================================================================================================================================


//===================================================================================================================================================
// КОНСТРУКТОРЫ КЛАССА
//===================================================================================================================================================

   explicit nx_array(size_type initial_count = 0, size_type reserve_count = 0)
   {
      static_assert(std::is_default_constructible<T>::value, "NxArray: T must be default-constructible");
      if(initial_count > max_size() || reserve_count > max_size())
         throw std::length_error("NxArray: requested size exceeds max_size()");
      internal_reserve(std::max(initial_count, reserve_count));
      data_list.resize(initial_count);
      rebuild_pointers();
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Конструкторы с инициализацией

   // инициализация дефолтным элементом
   // ! для массиво целочисленных типов обязательно приведение параметров функции к size_t:
   // ! например: nx_array<int> arr(size_t(5), size_t(10), 42); -- 5 элементов со значением 42, резерв = 10
   //explicit nx_array(size_type initial_count, size_type reserve_count, const T& default_value) noexcept(std::is_nothrow_copy_constructible<T>::value)
   //{
   //   if(initial_count > max_size()) throw std::length_error("NxArray: initial_count exceeds max_size()");
   //   if(reserve_count > max_size()) throw std::length_error("NxArray: reserve_count exceeds max_size()");

   //   if(reserve_count > initial_count)
   //   {
   //      ptr_vector.reserve(reserve_count);
   //      reserved_capacity = reserve_count;
   //   }

   //   for(size_type i = 0; i < initial_count; i++)
   //   {
   //     addItem(default_value, END_POS);
   //   }
   // }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // конструктор для списка инициализации
   // примеры:
   // nx_array<QString> arr1{"a", "b", "c"};                      // массив с тремя элементами
   // nx_array<QString> arr2({"a", "b", "c"}, 10);                // резерв = 10
   // nx_array<QString> arr3{{"a", "b", "c"}, 10};                // тоже самое

   explicit nx_array(std::initializer_list<T> init_list, size_type reserve_count = 0)
   {
      if(init_list.size() > max_size() || reserve_count > max_size())
         throw std::length_error("NxArray: requested size exceeds max_size()");
      internal_reserve(std::max(static_cast<size_type>(init_list.size()), reserve_count));
      assign(init_list.begin(), init_list.end());
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // базовый конструктор по итераторам (общая реализация)
   template <typename InputIt, typename = typename std::enable_if<!std::is_integral<InputIt>::value>::type>
   nx_array(InputIt first, InputIt last, size_type reserve_count = 0, bool to_move = false)
   {
      static_assert(std::is_base_of<std::input_iterator_tag, typename std::iterator_traits<InputIt>::iterator_category>::value,
                    "NxArray: InputIt must be an input iterator");
      if(reserve_count > max_size()) throw std::length_error("NxArray: reserve exceeds max_size()");
      internal_reserve(reserve_count);
      list_type pending(data_list.get_allocator());
      if(to_move)
      {
         auto move_first = std::make_move_iterator(first);
         auto move_last = std::make_move_iterator(last);
         stage_selected(pending, move_first, move_last, max_size(),
                        typename std::is_constructible<T, decltype(*move_first)>::type());
      }
      else stage_selected(pending, first, last, max_size(),
                          typename std::is_constructible<T, decltype(*first)>::type());
      replace_pending(pending);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из классов-контейнеров
   template <typename...>
   using void_t = void;

   template <typename Container,
             typename = void_t<typename Container::value_type,
                               decltype(std::declval<Container>().size()),
                               decltype(std::declval<Container>().begin()),
                               decltype(std::declval<Container>().end())>>
   explicit nx_array(const Container& container,
                     size_type reserve_count = 0,
                     bool to_move = false,
                     typename std::enable_if<std::is_class<Container>::value && !std::is_same<Container, T>::value>::type* = nullptr)
   {
      const auto source_size = container.size();
      // Check signed sizes before conversion; also valid for unsigned size types.
      if(source_size != 0 && source_size < 1)
         throw std::invalid_argument("NxArray: negative container size");
      if(static_cast<std::uintmax_t>(source_size) > static_cast<std::uintmax_t>(max_size()) || reserve_count > max_size())
         throw std::length_error("NxArray: requested size exceeds max_size()");
      const size_type count = static_cast<size_type>(source_size);
      internal_reserve(std::max(count, reserve_count));

      // обработка элементов
      try
      {
         if(to_move)
         {
            static_assert(std::is_move_constructible<T>::value, "NxArray: T must be move-constructible");
            for(auto it = container.begin(); it != container.end(); it++)
            {
               //emplaceItem(END_POS, std::move(const_cast<T&>(*it)));
               emplaceItem(END_POS, T(std::move(*it)));
            }
         }
         else assign(container.begin(), container.end());
      }
      catch(...)
      {
         this->clear();
         throw;
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из классов-контейнеров (перемещение по rvalue-ссылке)
   template <typename Container,
             typename std::enable_if<
                std::is_class<typename std::remove_reference<Container>::type>::value &&
                !std::is_lvalue_reference<Container>::value &&
                !std::is_same<typename std::remove_reference<Container>::type, nx_array>::value &&
                !std::is_same<typename std::remove_reference<Container>::type, list_type>::value,
                int>::type = 0>
   explicit nx_array(Container&& container)
{
       reserve(container.size());
       for (auto&& item : container) {
           emplace_back(std::move(item));
       }
   }

   // отдельно для std::list
   explicit nx_array(list_type&& lst, size_type reserve_count = 0)
   {
      reserve(std::max(lst.size(), reserve_count));
      internal_L_splice(size(), std::move(lst));
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из другого nx_array
   nx_array(const nx_array& other_array)
      : data_list(std::allocator_traits<Allocator>::select_on_container_copy_construction(other_array.get_allocator()))
   {
      if(other_array.size() > max_size()) throw std::length_error("NxArray: copy exceeds max_size()");
      reserve(std::min(other_array.capacity(), max_size()));
      data_list.insert(data_list.end(), other_array.data_list.begin(), other_array.data_list.end());
      rebuild_pointers();
      // Copying raw pointers does not transfer ownership.
   }

   explicit nx_array(nx_array& other_array, size_type reserve_count = 0, bool to_move = false)
   {
      reserve(reserve_count); // Finish the throwing allocation before taking ownership.
      if(to_move) *this = std::move(other_array);  // используем перемещение
      else *this = other_array;                     // используем копирование
   }

   template<class U, class A>
   explicit nx_array(nx_array<U,A>&& source, size_type reserve_count = 0, bool to_move = true)
   {
      reserve(reserve_count);
      if(to_move) assign(std::move(source));
      else copy_temporary(source);
   }

   template<class U, class A>
   explicit nx_array(const nx_array<U,A>&& source, size_type reserve_count = 0, bool to_move = false)
   {
      if(to_move && !source.empty()) throw std::invalid_argument("NxArray: cannot move from a const array");
      reserve(reserve_count);
      copy_temporary(source);
   }

   nx_array(nx_array&& other) noexcept(
      std::is_nothrow_move_constructible<decltype(data_list)>::value &&
      std::is_nothrow_move_constructible<decltype(ptr_vector)>::value) : data_list(std::move(other.data_list)), ptr_vector(std::move(other.ptr_vector)), reserved_capacity(other.reserved_capacity), isDeleteItemsOnDestroy(other.isDeleteItemsOnDestroy)
   {
      other.isDeleteItemsOnDestroy = false;
      other.internal_clear();
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из std::array с резервированием памяти и возможностью перемещения
   template <size_t N>
   explicit nx_array(const std::array<T, N>& arr, size_type reserve_count = 0, bool to_move = false) : nx_array(arr.begin(), arr.end(), std::max(N, reserve_count), to_move) {}
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из std::vector с резервированием памяти и возможностью перемещения
   explicit nx_array(const std::vector<T>& vec, size_type reserve_count = 0, bool to_move = false) : nx_array(vec.begin(), vec.end(), reserve_count, to_move) {}
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из std::list с резервированием памяти и возможностью перемещения
   explicit nx_array(const list_type& lst, size_type reserve_count = 0, bool to_move = false)
   {
      // A const reference may copy, but must never transfer list nodes.
      if(to_move) throw std::invalid_argument("NxArray: use std::move(non_const_list) to transfer nodes");
      reserve(std::max(lst.size(), reserve_count));
      assign(lst.begin(), lst.end());
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из std::deque с резервированием памяти и возможностью перемещения
   explicit nx_array(const std::deque<T>& deq, size_type reserve_count = 0, bool to_move = false) : nx_array(deq.begin(), deq.end(), reserve_count, to_move) {}

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из QList с резервированием памяти и возможностью перемещения
   explicit nx_array(const QList<T>& qlist, size_type reserve_count = 0, bool to_move = false) : nx_array(qlist.begin(), qlist.end(), reserve_count, to_move) {}
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // инициализация из простого массива с резервированием памяти и возможностью перемещения
   // примеры:
   //int raw_arr[] = {1, 2, 3};
   //nx_array<int> a(3, raw_arr);
   //nx_array<int> b(3, raw_arr, 100);     // с резервированием
   //nx_array<int> c(3, raw_arr, 0, true); // с перемещением
   //std::vector<int> vec = {1, 2, 3};
   //nx_array<int> d(3, vec.data());       // берем три элемента вектора через указатель на первый элемент

   explicit nx_array(size_type count, const T* arr, size_type reserve_count = 0, bool to_move = false)
   {
      if(count && to_move) throw std::invalid_argument("NxArray: cannot move from a const array");
      init_raw(count, arr, reserve_count, false);
   }

   // Deduction leaves nullptr and integer null constants to the const-pointer overload.
   template<class U, typename std::enable_if<std::is_same<U, T>::value, int>::type = 0>
   explicit nx_array(size_type count, U* arr, size_type reserve_count = 0, bool to_move = false)
   {
      init_raw(count, arr, reserve_count, to_move);
   }

private:
   template<class U>
   void init_raw(size_type count, U* arr, size_type reserve_count, bool to_move)
   {
      if(count && !arr) throw std::invalid_argument("NxArray: null source array");
      if(count > max_size() || reserve_count > max_size())
         throw std::length_error("NxArray: requested size exceeds max_size()");
      reserve(std::max(count, reserve_count));
      if(!count) return;
      list_type pending(data_list.get_allocator());
      if(to_move)
         stage_selected(pending, std::make_move_iterator(arr), std::make_move_iterator(arr + count),
                        count, typename std::is_constructible<T, U&&>::type());
      else stage_selected(pending, arr, arr + count, count, typename std::is_constructible<T, U&>::type());
      insert_pending(0, pending);
   }
public:
   void checkIterator(const const_iterator& it) const
   {
      if(it.container() != this) throw std::invalid_argument("Iterator does not belong to this container");
      if(it.index() > size()) throw std::out_of_range("Iterator out of range");
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   iterator begin() noexcept
   {
      return iterator(this, 0);
   }

   iterator end() noexcept
   {
      return iterator(this, this->size());
   }

   reverse_iterator rbegin() noexcept
   {
      return reverse_iterator(end());
   }

   reverse_iterator rend() noexcept
   {
      return reverse_iterator(begin());
   }

   const_iterator begin() const noexcept
   {
      return const_iterator(this, 0);
   }

   const_iterator end() const noexcept
   {
      return const_iterator(this, this->size());
   }

   const_iterator cbegin() const noexcept
   {
      return const_iterator(this, 0);
   }

   const_iterator cend() const noexcept
   {
      return const_iterator(this, this->size());
   }

   const_reverse_iterator rbegin() const noexcept
   {
      return const_reverse_iterator(end());
   }

   const_reverse_iterator rend() const noexcept
   {
      return const_reverse_iterator(begin());
   }

   const_reverse_iterator crbegin() const noexcept
   {
      return rbegin();
   }

   const_reverse_iterator crend() const noexcept
   {
      return rend();
   }

//===================================================================================================================================================
// ОПЕРАТОРЫ
//===================================================================================================================================================

//---------------------------------------------------------------------------------------------------------------------------------------------------
// ОПЕРАТОР []  -- ДОСТУП ПО ИНДЕКСУ
//---------------------------------------------------------------------------------------------------------------------------------------------------

   T& operator[](size_type index)
   {
      check_index(index);
      return *ptr_vector[index];
   }
//--

   const T& operator[](size_type index) const
   {
      check_index(index);
      return *ptr_vector[index];
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------

   friend random_access_iterator operator+(typename random_access_iterator::difference_type n, const random_access_iterator& it)
   {
      return it + n;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------------------------------------------------------------------
// ОПЕРАТОР =  -- ПРИСВАИВАНИЕ ЧЕРЕЗ СОЗДАНИЕ КОПИЙ
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // (создание копии другого nx_array)
   // Lvalue modifiers return a reference for chaining. Rvalue modifiers delegate
   // to the same operation, then return the whole container by move, preserving ownership.
   nx_array& operator=(const nx_array& other_array) &
   {
      if(this != &other_array)
      {
         check_copy_ownership(other_array);
         if(other_array.size() > max_size()) throw std::length_error("NxArray: copy exceeds max_size()");
         list_type new_data(other_array.data_list.begin(), other_array.data_list.end(), data_list.get_allocator());
         std::vector<T*> new_index;
         new_index.reserve(std::max(capacity(), other_array.size()));
         for(auto& item : new_data) new_index.push_back(std::addressof(item));
         data_list.swap(new_data);
         ptr_vector.swap(new_index);
         reserved_capacity = ptr_vector.capacity();
      }
      return *this;
   }

   nx_array operator=(const nx_array& other_array) &&
   {
      this->operator=(other_array);
      return std::move(*this);
   }

   nx_array& operator=(const nx_array&& other_array) &
   {
      check_temporary(other_array);
      return *this = static_cast<const nx_array&>(other_array);
   }

   nx_array operator=(const nx_array&& other_array) &&
   {
      this->operator=(std::move(other_array));
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template <size_t N>
   nx_array& operator=(const std::array<T, N>& arr) & {
       assign(arr.begin(), arr.end());
       return(*this);
   }

   template <size_t N>
   nx_array operator=(const std::array<T, N>& arr) &&
   {
      this->operator=(arr);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(const std::vector<T>& vec) &
   {
      assign(vec.begin(), vec.end());
      return *this;
   }

   nx_array operator=(const std::vector<T>& vec) &&
   {
      this->operator=(vec);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(const list_type& lst) &
   {
      assign(lst.begin(), lst.end());
      return *this;
   }

   nx_array operator=(const list_type& lst) &&
   {
      this->operator=(lst);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(const std::deque<T>& deq) &
   {
      assign(deq.begin(), deq.end());
      return *this;
   }

   nx_array operator=(const std::deque<T>& deq) &&
   {
      this->operator=(deq);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(const QList<T>& qlist) &
   {
      assign(qlist.begin(), qlist.end());
      return *this;
   }

   nx_array operator=(const QList<T>& qlist) &&
   {
      this->operator=(qlist);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // (создание копии из простого массива (указатель + размер))
   //
   // пример использования:
   // int raw_arr[] = {7, 8, 9};
   // nx_array<int> arr;
   // arr = {raw_arr, 3};  // через std::pair

   nx_array& operator=(const std::pair<const T*, size_type> arr_info) &
   {
      if(!arr_info.first && arr_info.second != 0) throw std::invalid_argument("NxArray: null source array");
      if(arr_info.second == 0) clear();
      else assign(arr_info.first, arr_info.first + arr_info.second);
      return *this;
   }

   nx_array operator=(const std::pair<const T*, size_type> arr_info) &&
   {
      this->operator=(arr_info);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------------------------------------------------------------------
// ОПЕРАТОР =std::move и <<=   -- ПЕРЕМЕЩЕНИЕ ЧЕРЕЗ RVALUE-ССЫЛКИ
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // ОПЕРАТОРЫ ПРИСВАИВАНИЯ

   // пример использования:
   // QList<QString> tempList = {"X", "Y", "Z"};
   // nx_array<QString> arr;
   // arr = std::move(tempList);  // вариант 1
   // arr <<= tempList            // вариант 2

   // перемещение из nx_array


//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(nx_array&& other_array) &
   {
      if(this != &other_array)
      {
         move_storage(other_array, typename std::allocator_traits<Allocator>::propagate_on_container_move_assignment());
         isDeleteItemsOnDestroy = other_array.isDeleteItemsOnDestroy;
         other_array.isDeleteItemsOnDestroy = false;
         other_array.clear();
      }
      return *this;
   }

   nx_array operator=(nx_array&& other_array) &&
   {
      this->operator=(std::move(other_array));
      return std::move(*this);
   }
//--

   nx_array& operator<<=(nx_array& other_array) &
   {
      return(*this = std::move(other_array));
   }

   nx_array operator<<=(nx_array& other_array) &&
   {
      this->operator<<=(other_array);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template <size_t N>
   nx_array& operator=(std::array<T, N>&& arr) &
   {
      assign(std::make_move_iterator(arr.begin()), std::make_move_iterator(arr.end()));
      return(*this);
   }

   template <size_t N>
   nx_array operator=(std::array<T, N>&& arr) &&
   {
      this->operator=(std::move(arr));
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(std::vector<T>&& vec) &
   {
      assign(std::make_move_iterator(vec.begin()), std::make_move_iterator(vec.end()));
      vec.clear();
      return *this;
   }

   nx_array operator=(std::vector<T>&& vec) &&
   {
      this->operator=(std::move(vec));
      return std::move(*this);
   }
//--

   nx_array& operator<<=(std::vector<T>& vec) &
   {
      return(*this = std::move(vec));
   }

   nx_array operator<<=(std::vector<T>& vec) &&
   {
      this->operator<<=(vec);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(list_type&& lst) &
   {
      if(lst.size() > max_size()) throw std::length_error("NxArray: replacement exceeds max_size()");
      list_type pending(std::move(lst), data_list.get_allocator());
      replace_pending(pending);
      return *this;
   }

   nx_array operator=(list_type&& lst) &&
   {
      this->operator=(std::move(lst));
      return std::move(*this);
   }
//--

   nx_array& operator<<=(list_type& lst) &
   {
      return(*this = std::move(lst));
   }

   nx_array operator<<=(list_type& lst) &&
   {
      this->operator<<=(lst);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(std::deque<T>&& deq) &
   {
      assign(std::make_move_iterator(deq.begin()), std::make_move_iterator(deq.end()));
      deq.clear();
      return *this;
   }

   nx_array operator=(std::deque<T>&& deq) &&
   {
      this->operator=(std::move(deq));
      return std::move(*this);
   }
//--

   nx_array& operator<<=(std::deque<T>& deq) &
   {
      return(*this = std::move(deq));
   }

   nx_array operator<<=(std::deque<T>& deq) &&
   {
      this->operator<<=(deq);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator=(QList<T>&& qlist) &
   {
      assign(std::make_move_iterator(qlist.begin()), std::make_move_iterator(qlist.end()));
      qlist.clear();
      return *this;
   }

   nx_array operator=(QList<T>&& qlist) &&
   {
      this->operator=(std::move(qlist));
      return std::move(*this);
   }
//--

   nx_array& operator<<=(QList<T>& qlist) &
   {
      return *this = std::move(qlist);
   }

   nx_array operator<<=(QList<T>& qlist) &&
   {
      this->operator<<=(qlist);
      return std::move(*this);
   }

   //---------------------------------------------------------------------------------------------------------------------------------------------------
   //================================================= ОПЕРАТОРЫ-ОБЕРТКИ ДЛЯ ДОБАВЛЕНИЯ ЭЛЕМЕНТОВ ======================================================
   //---------------------------------------------------------------------------------------------------------------------------------------------------

   //---------------------------------------------------------------------------------------------------------------------------------------------------
   // ОПЕРАТОР +=  -- ДОБАВЛЕНИЕ ЭЛЕМЕНТОВ ПУТЕМ КОПИРОВАНИЯ
   //---------------------------------------------------------------------------------------------------------------------------------------------------

   // пример использования:
   // nx_array<int> arr;
   // arr += 5; // добавление одного элемента
   // arr += std::vector<int>{1, 2, 3}; // добавление вектора

   // значение
   nx_array& operator+=(const T& value) &
   {
      addItem(value, END_POS);
      return(*this);
   }

   nx_array operator+=(const T& value) &&
   {
      this->operator+=(value);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // строковый литерал

   // для QString
   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array&>::type
   operator+=(const char* str) &
   {
      this->addItem(QString(str));
      return(*this);
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array>::type
   operator+=(const char* str) &&
   {
      this->operator+=(str);
      return std::move(*this);
   }

   // для std::string
   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array&>::type
   operator+=(const char* str) &
   {
      this->addItem(std::string(str));
      return(*this);
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array>::type
   operator+=(const char* str) &&
   {
      this->operator+=(str);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // другие массивы

   nx_array& operator+=(const nx_array& other_array) &
   {
      addArray(other_array, END_POS);
      return(*this);
   }

   nx_array operator+=(const nx_array& other_array) &&
   {
      this->operator+=(other_array);
      return std::move(*this);
   }

   // += copies. An owning temporary cannot supply borrowed pointers.
   nx_array& operator+=(nx_array&& other_array) &
   {
      check_temporary(other_array);
      return *this += static_cast<const nx_array&>(other_array);
   }

   nx_array operator+=(nx_array&& other_array) &&
   {
      this->operator+=(std::move(other_array));
      return std::move(*this);
   }

   nx_array& operator+=(const nx_array&& other_array) &
   {
      check_temporary(other_array);
      return *this += static_cast<const nx_array&>(other_array);
   }

   nx_array operator+=(const nx_array&& other_array) &&
   {
      this->operator+=(std::move(other_array));
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator+=(const std::vector<T>& vec) &
   {
      addArray(vec, END_POS);
      return(*this);
   }

   nx_array operator+=(const std::vector<T>& vec) &&
   {
      this->operator+=(vec);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template <size_t N>
   nx_array& operator+=(const std::array<T, N>& arr) &
   {
      addArray(arr);
      return(*this);
   }

   template <size_t N>
   nx_array operator+=(const std::array<T, N>& arr) &&
   {
      this->operator+=(arr);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator+=(const list_type& lst) &
   {
      addArray(lst, END_POS);
      return(*this);
   }

   nx_array operator+=(const list_type& lst) &&
   {
      this->operator+=(lst);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator+=(const std::deque<T>& deq) &
   {
      addArray(deq, END_POS);
      return(*this);
   }

   nx_array operator+=(const std::deque<T>& deq) &&
   {
      this->operator+=(deq);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator+=(const QList<T>& qlist) &
   {
      addArray(qlist, END_POS);
      return(*this);
   }

   nx_array operator+=(const QList<T>& qlist) &&
   {
      this->operator+=(qlist);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------------------------------------------------------------------
// ОПЕРАТОР <<  -- ДОБАВЛЕНИЕ ЭЛЕМЕНТОВ ПУТЕМ ПЕРЕМЕЩЕНИЯ
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // одиночный элемент

   nx_array& operator<<(T&& value) &
   {
      addItem(std::move(value), END_POS);
      return(*this);
   }

   nx_array operator<<(T&& value) &&
   {
      this->operator<<(std::move(value));
      return std::move(*this);
   }

   nx_array& operator<<(T& value) &
   {
      addItem(std::move(value), END_POS);
      return(*this);
   }

   nx_array operator<<(T& value) &&
   {
      this->operator<<(value);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // строковый литерал
   // (реализация без перемещения: аналог оператора +=)

   // для QString
   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array&>::type
   operator<<(const char* str) &
   {
      this->addItem(QString(str));  // копирование
      return(*this);  // цепочка вызовов
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array>::type
   operator<<(const char* str) &&
   {
      this->operator<<(str);
      return std::move(*this);
   }

   // для std::string
   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array&>::type
   operator<<(const char* str) &
   {
      this->addItem(std::string(str));  // копирование
      return(*this);  // цепочка вызовов
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array>::type
   operator<<(const char* str) &&
   {
      this->operator<<(str);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // массивы
   // реализация перемещения:

   // для nx_array и std::list  : через list.splice()
   // для одиночного объекта    : через list.insert(std::move())
   // для остальных контейнеров : через std::move()


   nx_array& operator<<(nx_array&& other_array) &
   {
      spliceArray(other_array);
      return(*this);
   }

   nx_array operator<<(nx_array&& other_array) &&
   {
      this->operator<<(std::move(other_array));
      return std::move(*this);
   }

   nx_array& operator<<(nx_array& other_array) &
   {
      if(this == &other_array) return *this;
      mergeArray(other_array);
      return(*this);
   }

   nx_array operator<<(nx_array& other_array) &&
   {
      this->operator<<(other_array);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator<<(list_type&& lst) &
   {
      spliceArray(lst);
      return(*this);
   }

   nx_array operator<<(list_type&& lst) &&
   {
      this->operator<<(std::move(lst));
      return std::move(*this);
   }

   nx_array& operator<<(list_type& lst) &
   {
      mergeArray(lst);
      return(*this);
   }

   nx_array operator<<(list_type& lst) &&
   {
      this->operator<<(lst);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator<<(std::vector<T>&& vec) &
   {
      addArray(std::move(vec), END_POS);
      return(*this);
   }

   nx_array operator<<(std::vector<T>&& vec) &&
   {
      this->operator<<(std::move(vec));
      return std::move(*this);
   }

   nx_array& operator<<(std::vector<T>& vec) &
   {
      mergeArray(vec, END_POS);
      return(*this);
   }

   nx_array operator<<(std::vector<T>& vec) &&
   {
      this->operator<<(vec);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template <size_t N>
   nx_array& operator<<(std::array<T, N>&& arr) &
   {
      addArray(std::move(arr));
      return(*this);
   }

   template <size_t N>
   nx_array operator<<(std::array<T, N>&& arr) &&
   {
      this->operator<<(std::move(arr));
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator<<(std::deque<T>&& deq) &
   {
      addArray(std::move(deq), END_POS);
      return(*this);
   }

   nx_array operator<<(std::deque<T>&& deq) &&
   {
      this->operator<<(std::move(deq));
      return std::move(*this);
   }

   nx_array& operator<<(std::deque<T>& deq) &
   {
      mergeArray(deq, END_POS);
      return(*this);
   }

   nx_array operator<<(std::deque<T>& deq) &&
   {
      this->operator<<(deq);
      return std::move(*this);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   nx_array& operator<<(QList<T>&& qlist) &
   {
      addArray(std::move(qlist), END_POS);
      return(*this);
   }

   nx_array operator<<(QList<T>&& qlist) &&
   {
      this->operator<<(std::move(qlist));
      return std::move(*this);
   }

   nx_array& operator<<(QList<T>& qlist) &
   {
      mergeArray(qlist, END_POS);
      return(*this);
   }

   nx_array operator<<(QList<T>& qlist) &&
   {
      this->operator<<(qlist);
      return std::move(*this);
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // добавление группы элементов
   // пример:
   // arr << {1, 2, 3};  // тоже самое, что arr << 1 << 2 << 3;  ~~  arr.addArray({1, 2, 3});

   nx_array& operator<<(std::initializer_list<T> ilist) &
   {
      addArray(ilist);
      return *this;
   }

   nx_array operator<<(std::initializer_list<T> ilist) &&
   {
      this->operator<<(ilist);
      return std::move(*this);
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------------------------------------------------------------------
//========= ОПЕРАТОРЫ-ОБЕРТКИ ДЛЯ ДОБАВЛЕНИЯ НЕСКОЛЬКИХ ЭЛЕМЕНТОВ ЧЕРЕЗ "+" =========================================================================

   // Оператор + для добавления одиночного элемента (копирование)
   nx_array operator+(const T& value) const &
   {
      nx_array result(*this); // Копируем текущий массив
      result.addItem(value);  // Добавляем элемент
      return result;
   }

   nx_array operator+(T& value) &
   {
      nx_array result(*this); // Копируем текущий массив
      result.addItem(value);  // Добавляем элемент
      return result;
   }

   // Оператор + для добавления одиночного элемента (перемещение)
   nx_array operator+(T&& value) &&
   {
      this->addItem(std::move(value));
      return std::move(*this);
   }
//--

   nx_array operator+(const nx_array& other) const &
   {
      nx_array result(*this); // копируем текущий массив
      result.addArray(other); // добавляем элементы другого массива
      return result;
   }

   nx_array operator+(nx_array&& other) const &
   {
      check_temporary(other);
      return *this + static_cast<const nx_array&>(other);
   }

   nx_array operator+(const nx_array&& other) const &
   {
      check_temporary(other);
      return *this + static_cast<const nx_array&>(other);
   }

   nx_array operator+(nx_array& other) &
   {
      nx_array result(*this); // копируем текущий массив
      result.addArray(other); // добавляем элементы другого массива
      return result;
   }

   nx_array operator+(nx_array&& other) &&
   {
      if(this != &other) spliceArray(other);
      return std::move(*this);
   }
//--

   // Добавление std::vector
   nx_array operator+(const std::vector<T>& vec) const &
   {
      nx_array result(*this);
      result.addArray(vec);
      return result;
   }

   nx_array operator+(std::vector<T>& vec) &
   {
       nx_array result(*this);
       result.addArray(vec);  // вызовет mergeArray, если T поддерживает перемещение
       return result;
   }

   nx_array operator+(std::vector<T>&& vec) && {
       nx_array result(std::move(*this));
       result.addArray(std::move(vec));  // перемещение элементов
       return result;
   }
//--

   // Добавление QList
   nx_array operator+(const QList<T>& qlist) const &
   {
      nx_array result(*this);
      result.addArray(qlist);
      return result;
   }

   nx_array operator+(QList<T>& qlist) &
   {
      nx_array result(*this);
      result.addArray(qlist);  // вызовет mergeArray, если T поддерживает перемещение
      return result;
   }

   nx_array operator+(QList<T>&& qlist) &&
   {
      nx_array result(std::move(*this));
      result.addArray(std::move(qlist));  // перемещение элементов
      return result;
   }
//--

   // initializer_list
   nx_array operator+(std::initializer_list<T> ilist) const &
   {
      nx_array result(*this);
      result.addArray(ilist);
      return result;
   }

   nx_array operator+(std::initializer_list<T> ilist) &&
   {
      addArray(ilist);
      return std::move(*this);
   }

   nx_array operator+(std::initializer_list<T> ilist) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + ilist;
   }
//--

   // std::list (list_type)
   nx_array operator+(const list_type& lst) const &
   {
      nx_array result(*this);
      result.addArray(lst);
      return result;
   }

   nx_array operator+(list_type& lst) &
   {
      nx_array result(*this);
      result.addArray(lst);  // может использовать mergeArray, если нужно
      return result;
   }

   nx_array operator+(list_type&& lst) &&
   {
      nx_array result(std::move(*this));
      result.spliceArray(lst);
      return result;
   }
//--

   // std::deque
   nx_array operator+(const std::deque<T>& deq) const &
   {
      nx_array result(*this);
      result.addArray(deq);
      return result;
   }

   nx_array operator+(std::deque<T>& deq) &
   {
      nx_array result(*this);
      result.addArray(deq);
      return result;
   }

   nx_array operator+(std::deque<T>&& deq) &&
   {
      nx_array result(std::move(*this));
      result.addArray(std::move(deq));
      return result;
   }
//--

   // для обычных массивов (указатель + размер)
   nx_array operator+(const std::pair<const T*, size_type>& arr_info) const &
   {
      nx_array result(*this);
      result.addArray(arr_info.first, arr_info.second);
      return result;
   }
//--

   // для строковых литералов

   // для QString
   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array>::type
   operator+(const char* str) const &
   {
      nx_array result(*this);
      result.addItem(QString(str));
      return result;
   }

   // для std::string
   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array>::type
   operator+(const char* str) const &
   {
      nx_array result(*this);
      result.addItem(std::string(str));  // const char* → std::string
      return result;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------







//---------------------------------------------------------------------------------------------------------------------------------------------------
// ЗАПРЕЩЕННЫЕ ФУНКЦИИ С ПАРАМЕТРОМ СТРОКОВОГО ЛИТЕРАЛА ДЛЯ ТИПА ДАННЫХ char*
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template<typename U = T>
   typename std::enable_if<std::is_same<U, char*>::value, nx_array&>::type
   operator<<(const char* str) & = delete;

   template<typename U = T>
   typename std::enable_if<std::is_same<U, char*>::value, nx_array>::type
   operator<<(const char* str) && = delete;

   template<typename U = T>
   typename std::enable_if<std::is_same<U, char*>::value, nx_array&>::type
   operator+=(const char* str) & = delete;

   template<typename U = T>
   typename std::enable_if<std::is_same<U, char*>::value, nx_array>::type
   operator+=(const char* str) && = delete;

   template<typename U = T>
   typename std::enable_if<std::is_same<U, char*>::value, nx_array>::type
   operator+(const char* str) const & = delete;






   // Complete rvalue addition overloads; no forwarding template changes overload ranking.
   nx_array operator+(const T& value) &&
   {
      addItem(value);
      return std::move(*this);
   }

   nx_array operator+(const T& value) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + value;
   }

   nx_array operator+(const nx_array& other) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + other;
   }

   nx_array operator+(const nx_array& other) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + other;
   }

   nx_array operator+(nx_array&& other) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + std::move(other);
   }

   nx_array operator+(const nx_array&& other) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + std::move(other);
   }

   nx_array operator+(const nx_array&& other) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + std::move(other);
   }

   nx_array operator+(const std::vector<T>& vec) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + vec;
   }

   nx_array operator+(const std::vector<T>& vec) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + vec;
   }

   nx_array operator+(const QList<T>& qlist) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + qlist;
   }

   nx_array operator+(const QList<T>& qlist) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + qlist;
   }

   nx_array operator+(const list_type& lst) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + lst;
   }

   nx_array operator+(const list_type& lst) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + lst;
   }

   nx_array operator+(const std::deque<T>& deq) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + deq;
   }

   nx_array operator+(const std::deque<T>& deq) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + deq;
   }

   nx_array operator+(const std::pair<const T*, size_type>& arr_info) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + arr_info;
   }

   nx_array operator+(const std::pair<const T*, size_type>& arr_info) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + arr_info;
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array>::type
   operator+(const char* str) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + str;
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, nx_array>::type
   operator+(const char* str) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + str;
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array>::type
   operator+(const char* str) &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + str;
   }

   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, nx_array>::type
   operator+(const char* str) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this) + str;
   }

//===================================================================================================================================================
//============================================================== МЕТОДЫ =============================================================================
//===================================================================================================================================================

   // Проверка валидности указателя (т.е. что указатель есть в ptr_vector)
   bool isPointerValid(const T* ptr) const
   {
      if(ptr == nullptr) return false;

      for(const T* p : ptr_vector)
      {
         if(p == ptr) return true;
      }

      return false;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Возвращение индекса элемента по указателю
   difference_type getItemIndex(const T* ptr) const
   {
      if(ptr == nullptr) return -1;

      for(size_type i = 0; i < ptr_vector.size(); i++)
      {
         if(ptr_vector[i] == ptr) return static_cast<difference_type>(i);
      }

      return -1;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Получить указатель на элемент
   T* getItemPtr(size_type index)
   {
      check_index(index);
      assert(ptr_vector[index] != nullptr && "NxArray: corrupted pointer in ptr_vector");
      return ptr_vector[index];
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Получить указатели на все элементы
   // (безопасная реализация метода data(): доступ к указателям без возможности менять ptr_vector, что нарушило бы структуру данных класса)
   const T* const* data() const noexcept
   {
      return ptr_vector.data();
   }

   // Запрет небезопасных версий
   T** data() = delete;
//---------------------------------------------------------------------------------------------------------------------------------------------------

   T& at(size_type index)
   {
      check_index(index);
      return (*this)[index];
   }
//--

   const T& at(size_type index) const
   {
      check_index(index);
      return (*this)[index];
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   allocator_type get_allocator() const noexcept
   {
      return data_list.get_allocator();
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   size_type max_size() const noexcept
   {
      return std::min<size_type>(data_list.max_size(), ptr_vector.max_size());
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // ОСНОВНЫЕ МЕТОДЫ

   // ДОБАВЛЕНИЕ ЭЛЕМЕНТА

   // копированием
   void addItem(const T& value, int position = END_POS, bool useExc = false)
   {
      if(position < END_POS || (position > START_POS && static_cast<size_type>(position) > size()))
      {
         if(useExc) throw std::out_of_range("NxArray: invalid position");
         position = END_POS;
      }
      emplaceItem(position, value);
   }
//--

   // перемещением
   void addItem(T&& value, int position = END_POS, bool useExc = false)
   {
      if(position < END_POS || (position > START_POS && static_cast<size_type>(position) > size()))
      {
         if(useExc) throw std::out_of_range("NxArray: invalid position");
         position = END_POS;
      }
      emplaceItem(position, std::move(value));
   }
//--

   // версии для строкового литерала

   // для QString
   template<typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, void>::type
   addItem(const char* value, int position = END_POS, bool useExc = false)
   {
      this->addItem(QString(value), position, useExc);
   }

   // для std::string
   template<typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, void>::type
   addItem(const char* value, int position = END_POS, bool useExc = false)
   {
      this->addItem(std::string(value), position, useExc);
   }
//--

   // удаленная версия для char* (запрещает строковые литералы для массива типа char*)
   template<typename U = T>
   typename std::enable_if<std::is_same<U, char*>::value, void>::type
   addItem(const char* value, int position = END_POS, bool useExc = false) = delete;

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Добавление элемента с инициализацией
   template <typename... Args>
   T& emplaceItem(int position = END_POS, Args&&... args)
   {
      if(position < END_POS || (position > 0 && static_cast<size_type>(position) > size()))
         throw std::out_of_range("NxArray: invalid position");
      return emplace_at(position == END_POS ? size() : static_cast<size_type>(position),
                        std::forward<Args>(args)...);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   void assign(size_type count, const T& value)
   {
      if(count > max_size()) throw std::length_error("NxArray: count exceeds max_size()");
      list_type pending(count, value, data_list.get_allocator());
      replace_pending(pending);
   }
//--

   template <class InputIt>
   void assign(InputIt first, InputIt last, typename std::enable_if<!std::is_integral<InputIt>::value>::type* = nullptr)
   {
      list_type pending(data_list.get_allocator());
      stage_range(pending, first, last, max_size());
      replace_pending(pending);
   }
//--

   void assign(const nx_array& other) { *this = other; }
   void assign(nx_array&& other) { *this = std::move(other); }

   template<class U, class A>
   void assign(nx_array<U,A>&& source) { move_values(source); }

   template<class U, class A>
   void assign(const nx_array<U,A>&& source) { copy_temporary(source); }

   void assign(std::initializer_list<T> ilist)
   {
      assign(ilist.begin(), ilist.end());
   }
//--

   // assign из контейнера
   // версия для lvalue-контейнеров
   template <typename Container>
   //void assign(const Container& cont, typename std::enable_if<std::is_same<decltype(cont.begin()), decltype(cont.end())>::value>::type* = nullptr)
   auto assign(const Container& cont) -> decltype(cont.begin(), cont.end(), void())
   {
      assign(cont.begin(), cont.end());
   }
//--

   // версия для rvalue-контейнеров
   template <typename Container>
   auto assign(Container&& cont) -> typename std::enable_if<!std::is_lvalue_reference<Container>::value, decltype(cont.begin(), cont.end(), void())>::type
   {
      if(static_cast<const void*>(this) == static_cast<const void*>(std::addressof(cont))) return;
      assign(std::make_move_iterator(cont.begin()), std::make_move_iterator(cont.end()));
      clear_source(cont, 0);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Перемещение элемента на другую позицию   
   void moveItem(size_type from, size_type to)
   {
      check_index(from);
      check_index(to);
      if(from == to) return;
      auto source = data_list.begin();
      auto dest = data_list.begin();
      std::advance(source, from);
      std::advance(dest, to);
      if(from < to) ++dest;
      data_list.splice(dest, data_list, source);
      if(from < to) std::rotate(ptr_vector.begin() + from, ptr_vector.begin() + from + 1, ptr_vector.begin() + to + 1);
      else std::rotate(ptr_vector.begin() + to, ptr_vector.begin() + from, ptr_vector.begin() + from + 1);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Обмен элементов на позициях i и j
   void swapItems(size_type i, size_type j)
   {
      check_index(i);
      check_index(j);
      if(i == j) return;
      if(i > j) std::swap(i, j);
      auto first = data_list.begin();
      auto second = data_list.begin();
      std::advance(first, i);
      std::advance(second, j);
      auto after_second = std::next(second);
      data_list.splice(first, data_list, second);
      data_list.splice(after_second, data_list, first);
      std::swap(ptr_vector[i], ptr_vector[j]);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   void removeItem(size_type index)
   {
      // проверка инвариантов
      if(data_list.size() != ptr_vector.size())
      {
         rebuild_pointers(REBUILD_FULL);
      }

      // Проверка границ
      check_index(index);

      try
      {
         // удаляем элемент из list
         auto it = data_list.begin();
         std::advance(it, index);
         data_list.erase(it);

         // удаляем указатель из vector
         internal_erase(index);
       }
      catch(...)
      {
         // восстановление состояния при ошибке
         rebuild_pointers(REBUILD_FULL);
         throw;
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Удаление заданного диапазона элементов   
   void removeRange(size_type start, size_type finish = END_POS)
   {
      check_index(start, 1);
      if(finish == static_cast<size_type>(END_POS) || finish >= size()) finish = size() - 1;
      if(start > finish) throw std::invalid_argument("NxArray: start must be <= finish");
      auto first = data_list.begin();
      std::advance(first, start);
      auto last = first;
      std::advance(last, finish - start + 1);
      data_list.erase(first, last);
      internal_erase_range(start, finish + 1);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Удаление элементов через итераторы -- аналогично std::vector.erase

   iterator erase(const_iterator pos)
   {
      checkIterator(pos);
      check_index(pos.index());
      if(data_list.size() != ptr_vector.size()) rebuild_pointers(REBUILD_FULL);    // проверка инвариантов

      // удаление из data_list
      size_type index = pos.index();
      auto it = data_list.begin();

      try
      {
         std::advance(it, index);
      }
      catch(...)
      {
         throw;           // контейнер не изменился, просто пробрасываем исключение
      }

      try
      {
         data_list.erase(it);
         if(std::is_trivially_destructible<T>::value) internal_erase(index);         // для тривиальных типов - простое удаление указателя
         else rebuild_pointers(REBUILD_REMOVE, index, 1);

         return iterator(this, index);
      }
      catch(...)
      {
         rebuild_pointers(REBUILD_FULL);
         throw;
      }
   }
//----------

   iterator erase(const_iterator first, const_iterator last)
   {
      checkIterator(first);
      checkIterator(last);
      if(first.index() > last.index()) throw std::invalid_argument("NxArray: reversed erase range");
      if(first == last) return iterator(this, first.index());

      if(data_list.size() != ptr_vector.size()) rebuild_pointers(REBUILD_FULL);   // проверка инвариантов


      size_type first_idx = first.index();
      size_type count = std::distance(first, last);

      auto first_it = data_list.begin();
      auto last_it = first_it;

      try
      {
         std::advance(first_it, first_idx);
         std::advance(last_it, first_idx + count);
      }
      catch(...)
      {
         throw;        // контейнер не изменился, просто пробрасываем исключение
      }

      try
      {
         // удаление из data_list
         data_list.erase(first_it, last_it);

         if(std::is_trivially_destructible<T>::value) internal_erase_range(first_idx, first_idx + count);  // для тривиальных типов
         else rebuild_pointers(REBUILD_REMOVE, first_idx, count);

         internal_S_try_shrink();
         return iterator(this, first_idx);
      }
      catch(...)
      {
         rebuild_pointers(REBUILD_FULL);
         throw;
      }
   }
//----------

   // удаление по значению
   iterator erase(const T& value)
   {
      auto it = std::find_if(begin(), end(), [&value](const T& item) { return item == value; });
      if(it != end()) return erase(it);
      return end();
   }
//--

   iterator erase(iterator pos)
   {
      return erase(const_iterator(pos));
   }
//--

   template <typename Compare>
   iterator erase(const T& value, Compare comp)
   {
      auto it = std::find_if(begin(), end(), [&value, &comp](const T& item) { return comp(item, value); });
      return it != end() ? erase(it) : end();
   }
//----------

   size_type erase_all(const T& value)
   {
      const size_type old_size = size();
      // Evaluate every comparison before deleting: value may refer to our own element.
      erase_if([&value](const T& item) { return item == value; });
      return old_size - size();
   }
//--

   // удаление по условию
   template <typename Predicate>
   iterator erase_if(Predicate pred)
   {
      std::vector<size_type> indices;
      indices.reserve(size() / 2);
      for(size_type i = 0; i < size(); ++i)
      {
         if(pred(*ptr_vector[i])) indices.push_back(i); // Allocation failures propagate before mutation.
      }
      if(indices.empty()) return end();
      auto it = data_list.begin();
      size_type index = 0;
      size_type next = 0;
      while(it != data_list.end() && next < indices.size())
      {
         if(index == indices[next])
         {
            it = data_list.erase(it);
            ++next;
         }
         else ++it;
         ++index;
      }
      rebuild_pointers(); // Existing capacity is sufficient after removal.
      if(ptr_vector.size() < ptr_vector.capacity() - ptr_vector.size())
      {
         // Compaction is optional after the erase has committed.
         try { shrink_to_fit(); } catch(const std::bad_alloc&) {}
      }
      return iterator(this, std::min(indices.front(), size()));
   }
/*
   difference_type erase(iterator first, iterator last)
   {
      auto dist = std::distance(first, last);
      erase(first, last);
      return dist;
   }
*/
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Сортировка

   // Базовый вариант -- аналог std::vector::sort()
   bool sort()
   {
      return sort(std::less<T>());
   }
//--

   // Вариант с компаратором
   // пример использования:
   // nx_array<int> arr = {3,1,4,2};
   // arr.sort([](int a, int b) { return a < b; });                                            // результат: {1, 2, 3, 4} ~ аналогично arr.sort()
   // arr.sort([](int a, int b) { return a > b; });                                            // результат: {4, 3, 2, 1}
   // arr.sort([](int a, int b) { if(a % 2 == b % 2) return a < b; return a % 2 < b % 2; });   // результат: {2, 4, 1, 3} -- от меньшего к большему; сначала четные, потом нечетные

   template <typename Compare>
   bool sort(Compare comp)
   {
      if(size() < 2) return true;
      typedef typename list_type::iterator node_iterator;
      try
      {
         std::vector<node_iterator> order;
         order.reserve(size());
         for(auto it = data_list.begin(); it != data_list.end(); ++it) order.push_back(it);
         std::stable_sort(order.begin(), order.end(),
            [&comp](node_iterator a, node_iterator b) { return comp(*a, *b); });
         // Reorder only after every comparison succeeds. Same-list splice preserves addresses.
         for(auto it : order) data_list.splice(data_list.end(), data_list, it);
         rebuild_pointers();
         return true;
      }
      catch(...) { return false; }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Проверка наличия элемента по значению
   bool contains(const T& value, bool* ok = 0) const noexcept
   {
      if(ok != 0) *ok = true;
      try
      {
         return std::find(data_list.begin(), data_list.end(), value) != data_list.end();
      }
      catch(...)
      {
         if(ok != 0) *ok = false;
         return false;
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Поиск по значению с возвратом индекса элемента
   difference_type find(const T& value, bool* ok = 0) const noexcept
   {
      if(ok != 0) *ok = true;
      try
      {
         auto it = std::find(data_list.begin(), data_list.end(), value);
         return it != data_list.end() ? std::distance(data_list.begin(), it) : -1;
      }
      catch(...)
      {
         if(ok != 0) *ok = false;
         return -1;
      }
   }
//----------
   // Поиск по значению с возвратом итератора
   iterator find_itr(const T& value, bool* ok = 0) noexcept
   {
      if(ok != 0) *ok = true;
      try
      {
         auto it = std::find(data_list.begin(), data_list.end(), value);
         return it != data_list.end() ? iterator(this, std::distance(data_list.begin(), it)) : end();
       }
      catch(...)
      {
         if(ok != 0) *ok = false;
         return end();
      }
   }
//--

   const_iterator find_itr(const T& value, bool* ok = 0) const noexcept
   {
      if(ok != 0) *ok = true;
      try
      {
         auto it = std::find(data_list.begin(), data_list.end(), value);
         if(it != data_list.end()) return const_iterator(this, std::distance(data_list.begin(), it));
         return cend();
      }
      catch(...)
      {
         if(ok != 0) *ok = false;
         return cend();
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Изменение размер массива с добавлением новых элементов
   void resize(size_type new_size)
   {
      if(new_size > max_size()) throw std::length_error("NxArray: new size exceeds max_size()");

      const size_type current_size = this->size();

      try
      {
         if(new_size < current_size) removeRange(new_size, current_size - 1); // уменьшение размера
         else if(new_size > current_size)
         {
            if(new_size > reserved_capacity) reserve(new_size);               // увеличение размера с резервированием

            // добавляем элементы через emplaceItem
            for(size_type i = current_size; i < new_size; i++)
            {
               emplaceItem(END_POS);
            }
         }
      }
      catch(...)
      {
         rebuild_pointers(REBUILD_FULL);
         throw;
      }
   }
//--

   // ...с элементом по умолчанию
   void resize(size_type new_size, const T& value)
   {
      if(new_size > max_size()) throw std::length_error("NxArray: new size exceeds max_size()");

      const size_type current_size = this->size();

      try
      {
         if(new_size < current_size) removeRange(new_size, current_size - 1); //  уменьшение размера
         else if(new_size > current_size)
         {
            // увеличение размера с резервированием
            if(new_size > reserved_capacity) reserve(new_size);

            // добавляем копии значения через addItem
            for(size_type i = current_size; i < new_size; i++)
            {
               addItem(value, END_POS);
            }
         }
      }
      catch(...)
      {
         rebuild_pointers(REBUILD_FULL);
         throw;
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // обратный порядок элементов
   void reverse() noexcept
   {
      if(data_list.size() < 2) return;
      data_list.reverse();
      std::reverse(ptr_vector.begin(), ptr_vector.end());
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Присвоение значения для диапазона элементов (по индексам)
   void setValue(const T& value, size_type start, size_type finish = END_POS)
   {
      static_assert(std::is_copy_assignable<T>::value, "NxArray: T must be copy assignable for setValue()");

      // Проверка инвариантов (может выбросить исключение)
      validate_invariants();

      // Обработка пустого контейнера
      if(this->empty()) return;  // ничего не делаем для пустого массива

      // Определение finish (как в removeRange)
      size_type actual_finish = finish;
      if(finish == static_cast<size_type>(END_POS) || finish >= this->size()) actual_finish = this->size() - 1;

      // Проверка границ start
      if(start >= this->size()) throw std::out_of_range("NxArray: start index out of range");

      // Проверка корректности диапазона
      if(start > actual_finish) throw std::invalid_argument("NxArray: start must be <= finish");

      // Присваивание значений
      for(size_type i = start; i <= actual_finish; i++)
      {
         (*this)[i] = value;
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Присвоение значения для диапазона элементов (через итераторы)
   void setValue(const T& value, const_iterator first, const_iterator last)
   {
      static_assert(std::is_copy_assignable<T>::value, "NxArray: T must be copy assignable for setValue()");
      checkIterator(first);
      checkIterator(last);
      if(first.index() > last.index()) throw std::invalid_argument("NxArray: reversed setValue range");
      for(size_type i = first.index(); i < last.index(); ++i) *ptr_vector[i] = value;
   }

   template <typename InputIt>
   typename std::enable_if<!std::is_integral<InputIt>::value &&
                           !std::is_same<InputIt, iterator>::value &&
                           !std::is_same<InputIt, const_iterator>::value, void>::type
   setValue(const T& value, InputIt first, InputIt last)
   {
      static_assert(std::is_copy_assignable<T>::value, "NxArray: T must be copy assignable for setValue()");

      validate_invariants();

      if(first == last) return;  // пустой диапазон

      try
      {
         // присваиваем через итераторы
         for(; first != last; first++)
         {
            *first = value;
         }
      }
      catch (...)
      {
         rebuild_pointers(REBUILD_FULL);
         throw;
      }
   }
//--

   // Присвоение значения для диапазона элементов: от итератора до конца
   void setValue(const T& value, iterator first)
   {
      setValue(value, first, end());
   }
//--

   void setValue(const T& value, const_iterator first)
   {
      setValue(value, first, cend());
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // преобразование в std::list

   // возвращает копию массива
   list_type toStdList() const &
   {
      return list_type(data_list.begin(), data_list.end());
   }
//--

   // версия с перемещением данных.
   // преобразование в std::list
   // возвращает оригинал std::list,
   // старые адреса элементов nx_array становятся адресами элементов переменной std::list (валидность указателей сохраняется),
   // массив nx_array обнуляется
   //
   // пример использования:
   //
   // nx_array<MyClass> arr;
   // arr.addItem(MyClass("A"));
   // arr.addItem(MyClass("B"));
   // std::list<MyClass> lst = std::move(arr).toStdList();    // перемещение данных: arr теперь пуст, lst владеет элементами
   // std::list<MyClass> lst = arr.ejectStdList();            // тоже самое, но без необходимости указывать rvalue-ссылку (std::move)

   // Const temporaries cannot give away owned pointees; copying them would dangle.
   list_type toStdList() const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this).toStdList();
   }

   list_type ejectStdList()
   {
      list_type result(data_list.get_allocator());
      result.splice(result.end(), data_list);
      internal_clear();
      return result;
   }
//--

   list_type toStdList() &&
   {
       return ejectStdList();
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------


   // преобразование в std::array

   // возвращает копию массива
   template <size_t N>
   std::array<T, N> toStdArray() const &
   {
      if(this->size() != N) throw std::length_error("NxArray: nx_array size doesn't match std::array size");

      std::array<T, N> result;
      try
      {
         std::copy(begin(), end(), result.begin());
      }
      catch(...)
      {
         throw;
      }
      return result;
   }

   template <size_t N>
   std::array<T, N> toStdArray() const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this).template toStdArray<N>();
   }

   // версия с перемещением данных
   template <size_t N>
   std::array<T, N> toStdArray() &&
   {
      if(this->size() != N) throw std::length_error("NxArray: nx_array size doesn't match std::array size");

      std::array<T, N> result;
      try
      {
         std::move(begin(), end(), result.begin());
         this->clear();
      }
      catch(...)
      {
         throw;
      }
      return result;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // преобразование в std::vector

   // возвращает копию массива (для тривиальных типов)
   template<typename U = T>
   typename std::enable_if<std::is_trivially_copyable<U>::value, std::vector<U>>::type
   toStdVector(size_type capacity = 0) const &
   {
      std::vector<T> result;
      result.reserve((std::max)(capacity, data_list.size()));
      result.assign(data_list.begin(), data_list.end());
      return result;
   }
//--

   // возвращает копию массива (для нетривиальных типов)
   template<typename U = T>
   typename std::enable_if<!std::is_trivially_copyable<U>::value, std::vector<U>>::type
   toStdVector(size_type capacity = 0) const &
   {
      std::vector<T> result;
      result.reserve((std::max)(capacity, data_list.size()));
      for(const auto& item : data_list)
      {
         result.push_back(item);
      }
      return result;
   }
//--

   // версия с перемещением данных
   template<typename U = T>
   std::vector<U> toStdVector(size_type capacity = 0) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this).template toStdVector<U>(capacity);
   }

   std::vector<T> toStdVector(size_type capacity = 0) &&
   {
      std::vector<T> result;
      result.reserve(std::max(capacity, data_list.size()));
      result.assign(std::make_move_iterator(data_list.begin()),
                    std::make_move_iterator(data_list.end()));
      clear();
      shrink_to_fit();
      return result;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // преобразование в std::deque

   // возвращает копию массива (для тривиальных типов)
   template<typename U = T>
   typename std::enable_if<std::is_trivially_copyable<U>::value, std::deque<U>>::type
   toStdDeque() const &
   {
      return std::deque<T>(data_list.begin(), data_list.end());
   }
//--

   // возвращает копию массива (для нетривиальных типов)
   template<typename U = T>
   typename std::enable_if<!std::is_trivially_copyable<U>::value, std::deque<U>>::type
   toStdDeque() const &
   {
      std::deque<T> result;
      for(const auto& item : data_list)
      {
         result.push_back(item);
      }
      return result;
   }
//--

   // версия с перемещением данных
   template<typename U = T>
   std::deque<U> toStdDeque() const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this).template toStdDeque<U>();
   }

   std::deque<T> toStdDeque() &&
   {
      std::deque<T> result;
      result.assign(std::make_move_iterator(data_list.begin()),
                    std::make_move_iterator(data_list.end()));
      this->clear();
      return result;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // преобразование в QList (возвращает копию)

   // возвращает копию массива (для тривиальных типов)
   template<typename U = T>
   typename std::enable_if<std::is_trivially_copyable<U>::value, QList<U>>::type
   toQList(size_type capacity = 0) const &
   {
      QList<T> result;
      result.reserve(qlist_capacity(capacity));
      std::copy(data_list.begin(), data_list.end(), std::back_inserter(result));
      return result;
   }
//--

   // возвращает копию массива (для нетривиальных типов)
   template<typename U = T>
   typename std::enable_if<!std::is_trivially_copyable<U>::value, QList<U>>::type
   toQList(size_type capacity = 0) const &
   {
      QList<T> result;
      result.reserve(qlist_capacity(capacity));
      for(const auto& item : data_list)
      {
         result.append(item);
      }
      return result;
   }
//--

   // версия с перемещением данных
   template<typename U = T>
   QList<U> toQList(size_type capacity = 0) const &&
   {
      check_temporary(*this);
      return static_cast<const nx_array&>(*this).template toQList<U>(capacity);
   }

   QList<T> toQList(size_type capacity = 0) &&
   {
      QList<T> result;
      result.reserve(qlist_capacity(capacity));
      // Qt 5 QList has append(value), but no append(first, last).
      for(auto& item : data_list) result.append(std::move(item));
      clear();
      shrink_to_fit();
      return result;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // модификация объектов

   template <typename Function>
   typename std::enable_if<std::is_convertible<decltype(std::declval<Function>()(std::declval<T&>())), void>::value, void>::type
   apply(Function func)
   {
      for(auto& item : data_list)
      {
         func(item);
      }
   }
//--

   template <typename Function>
   typename std::enable_if<std::is_convertible<decltype(std::declval<Function>()(std::declval<const T&>())), void>::value, void>::type
   apply(Function func) const
   {
      for(const auto& item : data_list)
      {
         func(item);
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // извлечение элемента с удалением
   /// ! извлекает элемент с удалением из контейнера
   /// ! тип T должен быть move-constructible (проверяется static_assert)

   T extract(size_type index)
   {
      static_assert(std::is_nothrow_move_constructible<T>::value, "NxArray: T must be nothrow move-constructible for extract()");

      validate_invariants();                         // проверка инвариантов
      check_index(index);                            // проверка валидности индекса

      auto it = data_list.begin();
      std::advance(it, index);
      T result = std::move(*it);                     // перемещаем значение

      data_list.erase(it);                           // удаляем элемент из списка
      rebuild_pointers(REBUILD_REMOVE, index, 1);    // обновляем указатели

      return result;
   }
//--

   // функция для итератора - аналог std::list::extract в C++17
   T extract(const_iterator pos)
   {
      static_assert(std::is_nothrow_move_constructible<T>::value, "NxArray: T must be nothrow move-constructible for extract()");

      validate_invariants();                         // проверка инвариантов
      checkIterator(pos);
      check_index(pos.index());                      // проверка валидности итератора

      // получаем итератор списка
      auto list_it = data_list.begin();
      std::advance(list_it, pos.index());      
      T result = std::move(*list_it);                // перемещаем значение

      data_list.erase(list_it);                      // удаляем элемент
      rebuild_pointers(REBUILD_REMOVE, pos.index(), 1);

      return result;
   }
//--

   // перегрузка для обычного итератора
   T extract(iterator pos)
   {
      return extract(const_iterator(pos));
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------

// МЕТОДЫ ПРИСОЕДИНЕНИЯ ДРУГИХ МАССИВОВ

   // Присоединение другого массива путем копирования элементов

   template <typename Container>
   void addArray(const Container& container, int position = END_POS)
   {
      insert_elements(container.begin(), container.end(), position);
   }
//--

   void addArray(const nx_array& other, int position = END_POS)
   {
      check_copy_ownership(other);
      addArray(other.data_list, position);
   }
//--

   template <size_t N>
   void addArray(const std::array<T, N>& arr, int position = END_POS)
   {
      if(position != END_POS && (position < 0 || static_cast<size_type>(position) > this->size())) throw std::out_of_range("NxArray: invalid position");

      insert_elements(arr.begin(), arr.end(), position);
   }
//--

   void addArray(const list_type& lst, int position = END_POS)
   {
      insert_elements(lst.begin(), lst.end(), position);
   }
//--

   void addArray(const std::vector<T>& vec, int position = END_POS)
   {
      insert_elements(vec.begin(), vec.end(), position);
   }
//--

   void addArray(const std::deque<T>& deq, int position = END_POS)
   {
      insert_elements(deq.begin(), deq.end(), position);
   }
//--

   void addArray(const QList<T>& qlist, int position = END_POS)
   {
      insert_elements(qlist.begin(), qlist.end(), position);
   }
//--

   void addArray(const T* arr, size_type size, int position = END_POS, bool useExc = false)
   {
      if(!arr && size != 0) throw std::invalid_argument("NxArray: null source array");
      if(size == 0) return;
      insert_elements(arr, arr + size, position, useExc);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Присоединение массива nx_array или std::list через std::list.splice с сохранением валидности указателей элементов

   // Explicitly transfer nodes, matching the lvalue operator<< overloads.
   void mergeArray(nx_array& other, int position = END_POS)
   {
      spliceArray(other, position);
   }

   void mergeArray(list_type& other, int position = END_POS)
   {
      spliceArray(other, position);
   }

   void spliceArray(nx_array& arr, int position = END_POS)
   {
      const size_type index = position == END_POS ? size() : static_cast<size_type>(position);
      if(index > size()) throw std::out_of_range("NxArray: invalid splice position");
      transfer_nodes(arr, index);
   }
//--

   void spliceArray(list_type& lst, int position = END_POS)
   {
      if(lst.empty()) return;

      // преобразуем position в size_type
      size_type insert_pos = (position == END_POS) ? this->size() : (position == START_POS) ? 0 : static_cast<size_type>(position);

      // проверяем корректность позиции
      if(insert_pos > this->size()) throw std::out_of_range("NxArray: invalid position in spliceArray");

      internal_L_splice(insert_pos, std::move(lst));
      lst.clear();
   }

   // При вызове addArray(std::move) для nx_array и std::list также будет вызвана функция spliceArray
   void addArray(nx_array&& arr, int position = END_POS)
   {
      if(arr.data_list.empty()) return;
      spliceArray(arr, position);
   }

   template<class U, class A>
   void addArray(nx_array<U,A>&& source, int position = END_POS)
   {
      append_transfer(source, position == END_POS ? size() : static_cast<size_type>(position));
   }

   template<class U, class A>
   void addArray(const nx_array<U,A>&& source, int position = END_POS)
   {
      check_temporary(source);
      insert_elements(source.begin(), source.end(), position);
   }
//--

   void addArray(list_type&& lst, int position = END_POS)
   {
      if(lst.empty()) return;
      spliceArray(lst, position);
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------

   // Присоединение массивов через std::move

   template <size_t N>
   void addArray(std::array<T, N>&& arr, int position = END_POS)
   {
      if(position != END_POS && (position < 0 || static_cast<size_type>(position) > this->size())) throw std::out_of_range("NxArray: invalid position");

      insert_elements(std::make_move_iterator(arr.begin()),
                      std::make_move_iterator(arr.end()),
                      position);
   }
//--

   void addArray(std::vector<T>&& vec, int position = END_POS)
   {
      insert_elements(std::make_move_iterator(vec.begin()), std::make_move_iterator(vec.end()), position, true);
      vec.clear();
   }
//--

   void mergeArray(std::vector<T>& vec, int position = END_POS)
   {
      addArray(std::move(vec), position);
   }
//---------------

   void addArray(std::deque<T>&& deq, int position = END_POS)
   {
      insert_elements(std::make_move_iterator(deq.begin()), std::make_move_iterator(deq.end()), position, true);
      deq.clear();
   }
//--

   void mergeArray(std::deque<T>& deq, int position = END_POS)
   {
      if(!deq.empty() && !data_list.empty() && std::addressof(deq[0]) == std::addressof(*data_list.begin())) return;

      addArray(std::move(deq), position);
   }
//---------------

   void addArray(QList<T>&& qlist, int position = END_POS)
   {
      insert_elements(std::make_move_iterator(qlist.begin()), std::make_move_iterator(qlist.end()), position, true);
      qlist.clear();
   }
//--

   void mergeArray(QList<T>& qlist, int position = END_POS)
   {
      if(!qlist.empty() && !data_list.empty() && std::addressof(qlist[0]) == std::addressof(*data_list.begin())) return;

      addArray(std::move(qlist), position);
   }
//---------------

   // Перемещение элементов простых массивов, поддерживающих std::move
   void mergeArray(T* arr, size_type size, int position = END_POS)
   {
      if(!arr && size != 0) throw std::invalid_argument("NxArray: null source array");
      if(size == 0) return;
      insert_elements(std::make_move_iterator(arr), std::make_move_iterator(arr + size), position, true);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // присоединение элементов других классов-контейнеров через итераторы

   template <typename Container>
   iterator addArray(const Container& cont, const_iterator position)
   {
      return(insert(position, cont.begin(), cont.end()));
   }

   iterator addArray(const nx_array& arr, const_iterator position)
   {
      check_copy_ownership(arr);
      return insert(position, arr.begin(), arr.end());
   }

   iterator addArray(nx_array&& arr, const_iterator position)
   {
      checkIterator(position);
      transfer_nodes(arr, position.index());
      return iterator(this, position.index());
   }

   template<class U, class A>
   iterator addArray(nx_array<U,A>&& source, const_iterator position)
   {
      checkIterator(position);
      append_transfer(source, position.index());
      return iterator(this, position.index());
   }

   template<class U, class A>
   iterator addArray(const nx_array<U,A>&& source, const_iterator position)
   {
      check_temporary(source);
      return insert(position, source.begin(), source.end());
   }

   // lля std::vector
   iterator addArray(const std::vector<T>& vec, const_iterator position) {
       return insert(position, vec.begin(), vec.end());
   }

   // для std::array
   template <size_t N>
   iterator addArray(const std::array<T, N>& arr, const_iterator position)
   {
      return insert(position, arr.begin(), arr.end());
   }

   // lля std::list
   iterator addArray(const std::list<T>& lst, const_iterator position) {
       return insert(position, lst.begin(), lst.end());
   }

   // lля std::deque
   iterator addArray(const std::deque<T>& deq, const_iterator position) {
       return insert(position, deq.begin(), deq.end());
   }

   // для QList
   iterator addArray(const QList<T>& qlist, const_iterator position)
   {
      return insert(position, qlist.begin(), qlist.end());
   }

   // версии для rvalue-ссылок

   template <typename Container>
   typename std::enable_if<!std::is_lvalue_reference<Container>::value, iterator>::type
   addArray(Container&& cont, const_iterator position) {
       return insert(position,
                    std::make_move_iterator(cont.begin()),
                    std::make_move_iterator(cont.end()));
   }

   template <size_t N>
   iterator addArray(std::array<T, N>&& arr, const_iterator position)
   {
      return insert(position,
                   std::make_move_iterator(arr.begin()),
                   std::make_move_iterator(arr.end()));
   }

   iterator addArray(QList<T>&& qlist, const_iterator position) {
      return insert(position,
                   std::make_move_iterator(qlist.begin()),
                   std::make_move_iterator(qlist.end()));
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // резервирование памяти для элементов внутреннего вектора
   void reserve(size_type new_capacity)
   {
      if(new_capacity > max_size()) throw std::length_error("NxArray: reserve exceeds max_size()");
      internal_reserve(new_capacity);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // зарезервированная память внутреннего вектора
   size_type capacity() const noexcept
   {
      return reserved_capacity;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   void shrink_to_fit()
   {
      if(ptr_vector.capacity() > ptr_vector.size())
      {
         std::vector<T*>(ptr_vector).swap(ptr_vector);
         reserved_capacity = ptr_vector.capacity();
      }
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   T pop_back()
   {
      if(data_list.empty()) throw std::out_of_range("NxArray: container is empty");

      T value = std::move(data_list.back());
      data_list.pop_back();

      if(!ptr_vector.empty()) ptr_vector.pop_back();

      return value;  // возвращаем удаленный элемент
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // insert -- аналог std::vector

   // Вставка одного элемента (копирование)
   iterator insert(const_iterator pos, const T& value)
   {
      checkIterator(pos);
      size_type index = pos.index();
      emplace_at(index, value);
      return iterator(this, index);
   }

   // Вставка одного элемента (перемещение)
   iterator insert(const_iterator pos, T&& value)
   {
      checkIterator(pos);
      size_type index = pos.index();
      emplace_at(index, std::move(value));
      return iterator(this, index);
   }

   // Вставка нескольких одинаковых элементов
   iterator insert(const_iterator pos, size_type count, const T& value)
   {
      checkIterator(pos);
      if(count > max_size() - size()) throw std::length_error("NxArray: count exceeds max_size()");
      list_type pending(count, value, data_list.get_allocator());
      insert_pending(pos.index(), pending);
      return iterator(this, pos.index());
   }

   // Вставка диапазона элементов
   template <class InputIt>
   typename std::enable_if<!std::is_integral<InputIt>::value, iterator>::type
   insert(const_iterator pos, InputIt first, InputIt last)
   {
      checkIterator(pos);
      list_type pending(data_list.get_allocator());
      stage_range(pending, first, last, max_size() - size());
      insert_pending(pos.index(), pending);
      return iterator(this, pos.index());
   }

   // Вставка строкового литерала

   // для QString
   template <typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, iterator>::type
   insert(const_iterator pos, const char* str)
   {
      return this->insert(pos, QString(str));
   }

   // для std::string
   template <typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, iterator>::type
   insert(const_iterator pos, const char* str)
   {
      return this->insert(pos, std::string(str));
   }


   // Вставка из initializer_list
   iterator insert(const_iterator pos, std::initializer_list<T> ilist)
   {
      return this->insert(pos, ilist.begin(), ilist.end());
   }

//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------

   void swap(nx_array& other)
   {
      if(!std::allocator_traits<Allocator>::propagate_on_container_swap::value &&
         data_list.get_allocator() != other.data_list.get_allocator())
         throw std::invalid_argument("NxArray: cannot swap unequal non-propagating allocators");
      // обмен данными между текущим объектом и other
      std::swap(data_list, other.data_list);
      std::swap(ptr_vector, other.ptr_vector);
      std::swap(reserved_capacity, other.reserved_capacity);
      std::swap(isDeleteItemsOnDestroy, other.isDeleteItemsOnDestroy);
   }

   // специализация для std::swap
   friend void swap(nx_array& lhs, nx_array& rhs)
   {
      lhs.swap(rhs);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // ОПЕРАТОРЫ СРАВНЕНИЯ

   friend bool operator==(const nx_array& lhs, const nx_array& rhs)
   {
      return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
   }

   friend bool operator!=(const nx_array& lhs, const nx_array& rhs)
   {
      return !(lhs == rhs);
   }

   friend bool operator<(const nx_array& lhs, const nx_array& rhs)
   {
      return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
   }

   friend bool operator<=(const nx_array& lhs, const nx_array& rhs)
   {
      return !(rhs < lhs);
   }

   friend bool operator>(const nx_array& lhs, const nx_array& rhs)
   {
      return rhs < lhs;
   }

   friend bool operator>=(const nx_array& lhs, const nx_array& rhs)
   {
      return !(lhs < rhs);
   }
//----------
   // Сравнение с другими типами массивов

   // Для std::vector
   bool operator==(const std::vector<T>& vec) const
   {
      return(this->size() == vec.size() && std::equal(begin(), end(), vec.begin()));
   }

   bool operator!=(const std::vector<T>& vec) const
   {
      return !(*this == vec);
   }

   // Для std::deque
   bool operator==(const std::deque<T>& deq) const
   {
      return(this->size() == deq.size() && std::equal(begin(), end(), deq.begin()));
   }

   bool operator!=(const std::deque<T>& deq) const
   {
      return !(*this == deq);
   }

   // Для QList
   bool operator==(const QList<T>& qlist) const
   {
      return(this->size() == qlist.size() && std::equal(begin(), end(), qlist.begin()));
   }

   bool operator!=(const QList<T>& qlist) const
   {
      return !(*this == qlist);
   }

   // Для std::array
   template <size_t N>
   bool operator==(const std::array<T, N>& arr) const
   {
      return(this->size() == N && std::equal(begin(), end(), arr.begin()));
   }

   template <size_t N>
   bool operator!=(const std::array<T, N>& arr) const
   {
      return !(*this == arr);
   }
//===================================================================================================================================================
//------------------------------------------------------------- МЕТОДЫ ДЛЯ T* -----------------------------------------------------------------------

   // проверка, что все указатели-элементы массива валидны
   bool validate_pointers() const
   {
      if(data_list.size() != ptr_vector.size()) return false;
      auto it = data_list.begin();
      for(size_type i = 0; i < ptr_vector.size(); ++i, ++it)
         if(ptr_vector[i] != std::addressof(*it)) return false;
      return true;
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // очистка с удалением объектов (T*)
   template<typename U = T>
   typename std::enable_if<can_delete<U>::value, void>::type
   clear_with_delete()
   {
      // Group aliases before deleting any pointee, without allocating in the destructor.
      std::sort(ptr_vector.begin(), ptr_vector.end(),
         [](T* a, T* b) { return std::less<T>()(*a, *b); });
      for(size_type i = ptr_vector.size(); i > 1; --i)
         if(*ptr_vector[i-1] == *ptr_vector[i-2]) *ptr_vector[i-1] = nullptr;
      rebuild_pointers(); // Restore the index before invoking pointee destructors.
      for(auto& item : data_list)
      {
         T old = item;
         item = nullptr;
         delete old;
      }
      data_list.clear();
      internal_clear();
   }

   // для не-указателей работает как обычный clear()
   template<typename U = T>
   typename std::enable_if<!can_delete<U>::value, void>::type
   clear_with_delete()
   {

      clear();
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   iterator erase_with_delete(const_iterator pos)
   {
      checkIterator(pos);
      check_index(pos.index());
      removeItem_with_delete(pos.index());
      return iterator(this, pos.index());
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   template<typename U = T>
   typename std::enable_if<can_delete<U>::value, void>::type
   removeItem_with_delete(size_type index)
   {
      check_index(index);

      delete_item(*ptr_vector[index], std::true_type());
      removeItem(index);
   }

   // для не-указателей работает как обычный removeItem()
   template<typename U = T>
   typename std::enable_if<!can_delete<U>::value, void>::type
   removeItem_with_delete(size_type index)
   {
      removeItem(index);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

private:
   void delete_item(T& item, std::true_type)
   {
      const T old = item;
      if(!old) return;
      for(auto& alias : data_list) if(alias == old) alias = nullptr;
      delete old;
   }
   void delete_range(size_type start, size_type finish, std::true_type)
   {
      if(start == 0 && finish + 1 == size()) { clear_with_delete(); return; }
      // Finish all allocations before clearing aliases or destroying any objects.
      std::unordered_set<T> selected;
      std::vector<T> order;
      for(size_type i = start; i <= finish; ++i)
      {
         T p = *ptr_vector[i];
         if(p && selected.insert(p).second) order.push_back(p);
      }
      for(auto& p : data_list) if(selected.count(p)) p = nullptr;
      for(T p : order) delete p;
      removeRange(start, finish);
   }

   void delete_range(size_type start, size_type finish, std::false_type) { removeRange(start, finish); }
public:
   void removeRange_with_delete(size_type start, size_type finish = END_POS)
   {
      check_index(start, 1);
      if(finish == static_cast<size_type>(END_POS) || finish >= size()) finish = size() - 1;
      if(start > finish) throw std::invalid_argument("NxArray: start must be <= finish");
      delete_range(start, finish, typename can_delete<T>::type());
   }

//===================================================================================================================================================

   // методы replace и replace_with_delete()

   // простая замена значения (для всех типов)
   template<typename U = T>
   typename std::enable_if<can_delete<U>::value, void>::type
   replace(size_type index, T new_item)
   {
      check_index(index);

      // находим позицию в списке
      auto it = data_list.begin();
      std::advance(it, index);

      // заменяем значение (указатель) в списке
      *it = new_item;

      // обновляем ptr_vector - храним адрес элемента списка
      ptr_vector[index] = std::addressof(*it);     // it - итератор, *it - значение (указатель), std::addressof(*it) - адрес этого значения
   }

   // перегрузка для не-указателей
   template<typename U = T>
   typename std::enable_if<!can_delete<U>::value, void>::type
   replace(size_type index, const T& new_item)
   {
      at(index) = new_item;
   }

   // перегрузка с перемещением для не-указателей
   template<typename U = T>
   typename std::enable_if<!can_delete<U>::value, void>::type
   replace(size_type index, T&& new_item)
   {
      at(index) = std::move(new_item);
   }

   // специализация для строк
   template<typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, void>::type
   replace(size_type index, const char* new_item)
   {
      at(index) = new_item;
   }

   template<typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, void>::type
   replace(size_type index, const char* new_item)
   {
      at(index) = QString::fromUtf8(new_item);
   }
//---------------------------------------------------------------------------------------------------------------------------------------------------

   // для указателей: замена с удалением старого объекта
   template<typename U = T>
   typename std::enable_if<can_delete<U>::value, void>::type
   replace_with_delete(size_type index, T new_item)
   {
      check_index(index);

      // получаем старый указатель
      T old_item = nullptr;
      if(ptr_vector[index] != nullptr) old_item = *(ptr_vector[index]);

      // удаляем старый объект, только если он существует и не совпадает с новым
      if(old_item != new_item) delete_item(*ptr_vector[index], std::true_type());

      // находим позицию в списке
      auto it = data_list.begin();
      std::advance(it, index);

      // заменяем значение
      *it = new_item;

      // обновляем ptr_vector
      ptr_vector[index] = std::addressof(*it);     // it - итератор, *it - значение (указатель), std::addressof(*it) - адрес этого значения
   }

   // для не-указателей: работает как обычный replace() (delete не вызывается)
   template<typename U = T>
   typename std::enable_if<!can_delete<U>::value, void>::type
   replace_with_delete(size_type index, const T& new_item)
   {
      // просто вызывает replace()
      replace(index, new_item);
   }

   // Перегрузка с перемещением для не-указателей
   template<typename U = T>
   typename std::enable_if<!can_delete<U>::value, void>::type
   replace_with_delete(size_type index, T&& new_item)
   {
      replace(index, std::move(new_item));
   }

   // специализация для строк
   template<typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, void>::type
   replace_with_delete(size_type index, const char* new_item)
   {
      replace(index, new_item);
   }

   template<typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, void>::type
   replace_with_delete(size_type index, const char* new_item)
   {
      replace(index, new_item);
   }

//===================================================================================================================================================

   void push_back(const T& value) { addItem(value, END_POS); }
   void push_back(T&& value) { addItem(std::move(value), END_POS); }
   void push_front(const T& value) { addItem(value, START_POS); }
   void push_front(T&& value) { addItem(std::move(value), START_POS); }

   template <typename... Args>
   void emplace_back(Args&&... args) { emplaceItem(END_POS, std::forward<Args>(args)...); }

   template <typename... Args>
   void emplace_front(Args&&... args) { emplaceItem(START_POS, std::forward<Args>(args)...); }

   template <class... Args>
   iterator emplace(const_iterator pos, Args&&... args)
   {
      checkIterator(pos);
      size_type index = pos.index();
      emplace_at(index, std::forward<Args>(args)...);
      return iterator(this, index);
   }

   // Like QList: total size, or number of elements equal to value.
   size_type count() const noexcept { return size(); }

   size_type count(const T& value) const
   {
      size_type result = 0;
      for(const auto& item : data_list)
         if(item == value) ++result;
      return result;
   }

   template<typename U = T>
   typename std::enable_if<std::is_same<U, QString>::value, size_type>::type
   count(const char* str) const { return count(QString::fromUtf8(str)); }

   template<typename U = T>
   typename std::enable_if<std::is_same<U, std::string>::value, size_type>::type
   count(const char* str) const { return count(std::string(str)); }

   template<typename Compare>
   size_type count(const T& value, Compare comp) const
   {
      size_type result = 0;
      for(const auto& item : data_list)
         if(comp(item, value)) ++result;
      return result;
   }

   size_type size() const noexcept { return ptr_vector.size(); }
   bool empty() const noexcept { return data_list.empty(); }
   void clear() noexcept { data_list.clear(); internal_clear(); }
   T& front() { return data_list.front(); }
   T& back() { return data_list.back(); }
   const T& front() const { return data_list.front(); }
   const T& back() const { return data_list.back(); }

};



// Definitions are required for reference/address use in C++11.
template<typename T, typename Allocator>
const int nx_array<T, Allocator>::START_POS;

template<typename T, typename Allocator>
const int nx_array<T, Allocator>::END_POS;

#endif // NX_ARRAY_H
