// INTERFACE /////////////////////////////////////
#include <algorithm>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>
#include <exception>

#include <string>
#include <fstream>
#include <cstdlib>


/*
* Мы реализуем стандартный класс для хранения кучи с возможностью доступа
* к элементам по индексам. Для оповещения внешних объектов о текущих значениях
* индексов мы используем функцию index_change_observer.
*/

template <class T, class Compare = std::less<T>>
class Heap {
public:
    using IndexChangeObserver =
        std::function<void(const T& element, size_t new_element_index)>;

    static constexpr size_t kNullIndex = static_cast<size_t>(-1);

    explicit Heap(
        Compare compare = Compare(),
        IndexChangeObserver index_change_observer = IndexChangeObserver()) :
        compare_(compare),
        index_change_observer_(index_change_observer) {}

    size_t push(const T& value) {
        elements_.push_back(value);
        NotifyIndexChange(value, size() - 1);
        return SiftUp(size() - 1);
    }

    void erase(size_t index) {
        if (index != size() - 1) {
            SwapElements(index, size() - 1);
            NotifyIndexChange(elements_[size() - 1], kNullIndex);
            elements_.pop_back();
            SiftDown(index);
            SiftUp(index);
        }
        else if (index == size() - 1) {
            NotifyIndexChange(elements_[size() - 1], kNullIndex);
            elements_.pop_back();
        }
    }

    const T& top() const {
        return elements_[0];
    }

    void pop() {
        erase(0);
        return;
    }

    size_t size() const {
        return elements_.size();
    }

    bool empty() const {
        return elements_.empty();
    }

private:
    IndexChangeObserver index_change_observer_;
    Compare compare_;
    std::vector<T> elements_;

    size_t Parent(size_t index) const {
        return (index - 1) / 2;
    }

    size_t LeftSon(size_t index) const {
        return 2 * index + 1;
    }

    size_t RightSon(size_t index) const {
        return 2 * index + 2;
    }

    bool CompareElements(size_t first_index, size_t second_index) const {
        return compare_(elements_[first_index], elements_[second_index]);
    }

    void NotifyIndexChange(const T& element, size_t new_element_index) {
        index_change_observer_(element, new_element_index);
    }

    void SwapElements(size_t first_index, size_t second_index) {
        std::swap(elements_[first_index], elements_[second_index]);
        NotifyIndexChange(elements_[first_index], first_index);
        NotifyIndexChange(elements_[second_index], second_index);
    }

    size_t SiftUp(size_t index) {
        if (index == 0) {
            return index;
        }
        while ((index != 0) && (CompareElements(index, Parent(index)))) {
            SwapElements(index, Parent(index));
            index = Parent(index);
        }
        return index;
    }

    void SiftDown(size_t index) {
        if (index + 1 == size()) {
            return;
        }
        size_t leftIndex = LeftSon(index);
        size_t rightIndex = RightSon(index);

        while (leftIndex < elements_.size()) {
            size_t sonIndex = leftIndex;
            if (rightIndex < elements_.size() && CompareElements(rightIndex, leftIndex))
                sonIndex = rightIndex;

            if (CompareElements(index, sonIndex)) {
                return;
            }

            SwapElements(index, sonIndex);
            index = sonIndex;

            leftIndex = LeftSon(index);
            rightIndex = RightSon(index);
        }
    }
};

using DefaultHeap = Heap<int, std::less<int>>;

struct MemorySegment {
    int left;
    int right;
    size_t heap_index;

    MemorySegment(int left, int right) :
        left(left),
        right(right),
        heap_index(DefaultHeap::kNullIndex) {}

    size_t Size() const {
        return right - left + 1;
    }

    MemorySegment Unite(const MemorySegment& other) const {

        int uniteLeft = std::min(left, other.left);
        int uniteRight = std::max(right, other.right);
        return MemorySegment(uniteLeft, uniteRight);
    }
};

using MemorySegmentIterator = std::list<MemorySegment>::iterator;
using MemorySegmentConstIterator = std::list<MemorySegment>::const_iterator;


struct MemorySegmentSizeCompare {
    bool operator() (MemorySegmentIterator first,
        MemorySegmentIterator second) const {
        return std::make_pair(first->Size(), -first->left) >
               std::make_pair(second->Size(), -second->left);
    }
};


using MemorySegmentHeap =
Heap<MemorySegmentIterator, MemorySegmentSizeCompare>;


struct MemorySegmentsHeapObserver {
    void operator() (MemorySegmentIterator segment, size_t new_index) const
    {
        segment->heap_index = new_index;
    }
};

/*
* Мы храним сегменты в виде двухсвязного списка (std::list).
* Быстрый доступ к самому левому из наидлиннейших свободных отрезков
* осуществляется с помощью кучи, в которой (во избежание дублирования
* отрезков в памяти) хранятся итераторы на список — std::list::iterator.
* Чтобы быстро определять местоположение сегмента в куче для его изменения,
* мы внутри сегмента в списке храним heap_index, актуальность которого
* поддерживаем с помощью index_change_observer. Мы не храним отдельной метки
* для маркировки занятых сегментов: вместо этого мы кладём в heap_index
* специальный kNullIndex.
*/

class MemoryManager {
public:
    using Iterator = MemorySegmentIterator;
    using ConstIterator = MemorySegmentConstIterator;

    explicit MemoryManager(size_t memory_size) :
        free_memory_segments_(MemorySegmentSizeCompare(),
            MemorySegmentsHeapObserver()) {
        memory_segments_.push_back(MemorySegment(1, memory_size));
        free_memory_segments_.push(memory_segments_.begin());
    }

    Iterator Allocate(size_t size) {
        if (free_memory_segments_.size() == 0) {
            return end();
        }
        Iterator topElement = free_memory_segments_.top();
        if (topElement->Size() < size) {
            return end();
        }
        if (topElement->Size() == size) {
            free_memory_segments_.pop();
            return topElement;
        }

        free_memory_segments_.pop();
        MemorySegment newSegment(topElement->left, topElement->left + size - 1);
        topElement->left = newSegment.right + 1;
        Iterator newSegmentIterator = memory_segments_.insert(topElement, newSegment);
        free_memory_segments_.push(topElement);
        return newSegmentIterator;
    }

    void Free(Iterator position) {
        if (position != memory_segments_.begin()) {
            AppendIfFree(position, std::prev(position));
        }
        if (std::next(position) != memory_segments_.end()) {
            AppendIfFree(position, std::next(position));
        }
        free_memory_segments_.push(position);
    }

    Iterator end() {
        return memory_segments_.end();
    }

    ConstIterator end() const {
        return memory_segments_.cend();
    }

private:
    MemorySegmentHeap free_memory_segments_;
    std::list<MemorySegment> memory_segments_;

    void AppendIfFree(Iterator remaining, Iterator appending) {
        if (appending->heap_index != MemorySegmentHeap::kNullIndex) {
            MemorySegment augmentedSegment = remaining->Unite(*appending);
            *remaining = augmentedSegment;
            free_memory_segments_.erase(appending->heap_index);
            memory_segments_.erase(appending);
        }
    }
};


size_t ReadMemorySize(std::istream& stream = std::cin) {
    size_t memory_size;
    stream >> memory_size;
    return memory_size;
}

struct AllocationQuery {
    size_t allocation_size;
};

struct FreeQuery {
    int allocation_query_index;
};

/*
* Для хранения запросов используется специальный класс-обёртка
* MemoryManagerQuery. Фишка данной реализации в том, что мы можем удобно
* положить в него любой запрос, при этом у нас есть методы, которые позволят
* гарантированно правильно проинтерпретировать его содержимое. При реализации
* нужно воспользоваться тем фактом, что dynamic_cast возвращает nullptr
* при неудачном приведении указателей.
*/



class MemoryManagerQuery {
public:
    explicit MemoryManagerQuery(AllocationQuery allocation_query) :
        query_(new ConcreteQuery<AllocationQuery>(allocation_query)) {}

    explicit MemoryManagerQuery(FreeQuery free_query) :
        query_(new ConcreteQuery<FreeQuery>(free_query)) {}

    const AllocationQuery* AsAllocationQuery() const
    {
        auto ptr =
            dynamic_cast<ConcreteQuery<AllocationQuery> *>(query_.get());
        if (ptr)
            return &ptr->body;
        else
            return nullptr;
    }
    const FreeQuery* AsFreeQuery() const {
        auto ptr =
            dynamic_cast<ConcreteQuery<FreeQuery> *>(query_.get());
        if (ptr)
            return &ptr->body;
        else
            return nullptr;
    }

private:
    class AbstractQuery {
    public:
        virtual ~AbstractQuery() {
        }

    protected:
        AbstractQuery() {
        }
    };

    template <typename T>
    struct ConcreteQuery : public AbstractQuery {
        T body;

        explicit ConcreteQuery(T _body)
            : body(std::move(_body)) {
        }
    };

    std::unique_ptr<AbstractQuery> query_;
};

std::vector<MemoryManagerQuery> ReadMemoryManagerQueries(std::istream& stream = std::cin) {
    size_t queries_size;
    stream >> queries_size;
    std::vector<MemoryManagerQuery> queries;
    for (size_t current_query = 0; current_query < queries_size; ++current_query) {
        int abstract_query;
        stream >> abstract_query;
        if (abstract_query > 0) {
            size_t allocation_query = abstract_query;
            queries.emplace_back(AllocationQuery{ allocation_query });
        }
        if (abstract_query < 0) {
            queries.emplace_back(FreeQuery{ -abstract_query });
        }
    }
    return queries;
}

struct MemoryManagerAllocationResponse {
    bool success;
    size_t position;
};

/*
Мы предоставляем два builder'а - MakeSuccessfulAllocation и
MakeFailedAllocation. Они позволяют создавать корректные объекты
MemoryManagerAllocationResponse и в коде надо пользоваться ими. Но если
захочется как-то по-особенному инициализировать поля структуры (например, для
тестирования), то можно создать структуру с конкретными значениями полей.
*/
MemoryManagerAllocationResponse MakeSuccessfulAllocation(size_t position) {
    MemoryManagerAllocationResponse response;
    response.success = true;
    response.position = position;
    return response;
};

MemoryManagerAllocationResponse MakeFailedAllocation() {
    MemoryManagerAllocationResponse response;
    response.success = false;
    response.position = 0;
    return response;
}

std::vector<MemoryManagerAllocationResponse> RunMemoryManager(
    size_t memory_size, const std::vector<MemoryManagerQuery>& queries) {

    std::vector<MemoryManagerAllocationResponse> responses;
    std::vector<MemorySegmentIterator> iterators;
    MemoryManager memory(memory_size);
    for (size_t current_query = 0; current_query < queries.size(); ++current_query) {
        if (auto allocation_query = queries[current_query].AsAllocationQuery()) {
            MemorySegmentIterator newSegmentIterator =
                memory.Allocate(allocation_query->allocation_size);
            if (newSegmentIterator != memory.end()) {
                responses.push_back(MakeSuccessfulAllocation(newSegmentIterator->left));
                iterators.push_back(newSegmentIterator);
            } else {
                responses.push_back(MakeFailedAllocation());
                iterators.push_back(newSegmentIterator);
            }
        }
        else if (auto free_query = queries[current_query].AsFreeQuery()) {
            if (iterators[free_query->allocation_query_index - 1] != memory.end()) {
                memory.Free(iterators[free_query->allocation_query_index - 1]);
                iterators[free_query->allocation_query_index - 1] = memory.end();
            }
            iterators.push_back(memory.end());
        } else {
            throw std::runtime_error("Unknown MemoryManagerQuery type");
        }
    }
    return responses;
}

void OutputMemoryManagerResponses(const std::vector<MemoryManagerAllocationResponse>& responses,
    std::ostream& ostream = std::cout) {
    for (size_t current_response = 0; current_response < responses.size(); ++current_response) {
        if (responses[current_response].success == true) {
            ostream << responses[current_response].position << "\n";
        } else {
            ostream << -1 << "\n";
        }
    }
    std::cout << std::endl;
}


int main() {

    std::istream& input_stream = std::cin;
    std::ostream& output_stream = std::cout;
    const size_t memory_size = ReadMemorySize(input_stream);
    const std::vector<MemoryManagerQuery> queries =
        ReadMemoryManagerQueries(input_stream);
    const std::vector<MemoryManagerAllocationResponse> responses =
        RunMemoryManager(memory_size, queries);

    OutputMemoryManagerResponses(responses, output_stream);
    return 0;
}
