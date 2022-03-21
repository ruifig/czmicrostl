#include "czmut/czmut.h"
#include <string.h>
#include <stdlib.h>

// Provide my own allocator
#define CZ_VECTOR_UNITTEST_ALLOCATOR 1

#if CZ_VECTOR_UNITTEST_ALLOCATOR
    namespace cz::detail
    {
        // Allocator with simple tracking that doesn't stl or fancy, so it minimizes dependencies
        struct VectorAllocator
        {
            static constexpr int maxAllocs = 20;
            struct Info
            {
                 void* ptr;
                 size_t size;
            };

            static Info allocs[maxAllocs];

            VectorAllocator()
            {
                 memset(allocs, 0, sizeof(allocs));
            }

            static size_t _calcBytesAllocated()
            {
                 size_t total = 0;
                 for (auto&& slot : allocs)
                 {
                     total += slot.size;
                 }
                 return total;
            }

            static size_t _calcAllocations()
            {
                 size_t total = 0;
                 for (auto&& slot : allocs)
                 {
                     if (slot.ptr)
                     {
                         total++;
                     }
                 }
                 return total;
            }

            static void* _alloc(size_t bytes)
            {
                 Info* slot = getFreeSlot();
                 slot->ptr = malloc(bytes);
                 CHECK(slot->ptr);
                 slot->size = bytes;
                 return slot->ptr;
            }

            static void _free(void* ptr)
            {
                 Info* slot = getUsedSlot(ptr);
                 free(slot->ptr);
                 slot->ptr = nullptr;
                 slot->size = 0;
            }
        protected:

            static Info* getFreeSlot()
            {
                 for (auto&& slot : allocs)
                 {
                     if (!slot.ptr)
                     {
                         return &slot;
                     }
                 }
                 CHECK(false);
                 return nullptr; 
            }

            static Info* getUsedSlot(void* ptr)
            {
                 for (auto&& slot : allocs)
                 {
                     if (slot.ptr == ptr)
                     {
                         return &slot;
                     }
                 }
                 CHECK(false);
                 return nullptr;
            }

        };
        
        struct VectorAllocatorScopedCheck
        {
            VectorAllocatorScopedCheck()
            {
                 CHECK(VectorAllocator::_calcAllocations()==0);
                 CHECK(VectorAllocator::_calcBytesAllocated()==0);
            }
            ~VectorAllocatorScopedCheck()
            {
                 CHECK(VectorAllocator::_calcAllocations()==0);
                 CHECK(VectorAllocator::_calcBytesAllocated()==0);
            }
        };

        VectorAllocator::Info VectorAllocator::allocs[VectorAllocator::maxAllocs]; \
    	
        class VectorTestCase : public ::cz::mut::detail::TestCase
        {
        public:
            using TestCase::TestCase;
            virtual void onEnter() override
            {
                 CHECK(VectorAllocator::_calcAllocations()==0);
                 CHECK(VectorAllocator::_calcBytesAllocated()==0);
            }
            
            virtual void onExit() override
            {
                 CHECK(VectorAllocator::_calcAllocations()==0);
                 CHECK(VectorAllocator::_calcBytesAllocated()==0);
            }
        };
    } // namespace cz::detail

#else
	namespace cz::detail
    {
        using VectorTestCase = cz::mut::TestCase;
    }
#endif

#define VECTOR_TEST_CASE(Description) \
	CUSTOM_TEMPLATED_TEST_CASE(cz::detail::VectorTestCase, Description, "[vector]", int, Foo)

#include "impl/vector.h"

//
// Setup things that tells us if we can test behavior against STL
#ifdef _WIN32
	// If running on windows, it's a development environment, so we have STL
	#define HAS_STL 1
#else
	// Any other platform is considered as not having STL
	#define HAS_STL 0
#endif

#if HAS_STL
	#include <vector>
#endif

// Putting these inside a named namespace instead of anonymous namespace, because Visual Studio's debugger has problems
// with symbols in anonymous namespaces
namespace czvectortests
{

    struct FooCounters
    {
        int valueCounter;
        int destructor;
        int defaultConstructor;
        int constructor;
        int copyConstructor;
        int moveConstructor;
        int constructorExtra;
        int assigned;
        int moveAssigned;

        FooCounters()
        {
            memset(this, 0, sizeof(*this));
        }

        int totalCreated() const
        {
            return defaultConstructor + constructor + copyConstructor + moveConstructor + constructorExtra;
        }

        int alive() const
        {
            return totalCreated() - destructor;
        }

        void reset()
        {
            memset(this, 0, sizeof(*this));
        }

        bool operator==(const FooCounters& other) const
        {
            return memcmp(this, &other, sizeof(*this)) == 0 ? true : false;
        }
    } gCounter;
    	
    struct Foo
    {
        template<typename... Args>
        void log(Args&&... args)
        {
            //printf(std::forward<Args>(args)...);
        }

        Foo()
        {
            a = ++gCounter.valueCounter;
            log("%p: Default Constructor (%d)\n", this, a);
            ++gCounter.defaultConstructor;
        }

        ~Foo()
        {
            log("%p: Destructor(%d)\n", this, a);
            ++gCounter.destructor;
        }

        explicit Foo(int a) : a(a)
        {
            log("%p: Constructor(%d)\n", this, a);
            ++gCounter.constructor;
        }

        explicit Foo(int a, int dummy) : a(a)
        {
            log("%p: Constructor(%d, %d)\n", this, a, dummy);
            ++gCounter.constructorExtra;
        }

        Foo(const Foo& other) : a(other.a)
        {
            log("%p: Copy constructor(%d)\n", this, other.a);
            ++gCounter.copyConstructor;
        }

        Foo(Foo&& other) noexcept : a(other.a)
        {
            log("%p: Move constructor(%d)\n", this, other.a);
            other.a = 0;
            ++gCounter.moveConstructor;
        }

    	operator int() const
    	{
        	return a;
    	}

        bool operator==(int other) const
        {
            return a == other;
        }

        bool operator!=(int other) const
        {
            return a != other;
        }

        bool operator==(const Foo& other) const
        {
            return a == other.a;
        }

        bool operator!=(const Foo& other) const
        {
            return a != other.a;
        }

        Foo& operator=(const Foo& other)
        {
            if (this != &other)
            {
                 log("%p: assigned (%d) = (%d)\n", this, a, other.a);
                 a = other.a;
                 ++gCounter.assigned;
            }
            return *this;
        }

        Foo& operator=(Foo&& other)
        {
            if (this != &other)
            {
                 log("%p: move assigned (%d) = (%d)\n", this, a, other.a);
                 a = other.a;
                 other.a = 0;
                 ++gCounter.moveAssigned;
            }
            return *this;
        }

        int a;
    };
}

// We only check object counters if using vectors of Foo
#define CHECKFOO(expr) \
    if constexpr (std::is_same_v<TestType, Foo>) { CHECK(expr) }

#define CREATE_DEFAULT_VECTOR(v, count) \
    vector<TestType> v(count); \
	if constexpr(std::is_same_v<TestType, int>) \
	{ \
        int val = 1; \
        for(auto&& i : v) { i = val; val++; } \
    }

using namespace czvectortests;
using namespace cz;

VECTOR_TEST_CASE("Constructors")
{
	gCounter.reset();

	SECTION("default constructor")
	{
		vector<TestType> v;
		CHECK(v.size() == 0);
		CHECK(v.capacity() == 0);
        CHECKFOO(gCounter.totalCreated() == 0);
	}

	SECTION("Construct with N default elements")
	{
		CREATE_DEFAULT_VECTOR(v, 3);
		CHECK(v.size() == 3);
		CHECKFOO(gCounter.totalCreated() == 3 && gCounter.defaultConstructor == 3);
		CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
	}

	SECTION("Construct with N copies")
	{
		vector<TestType> v(3, TestType(2));
		CHECK(v.size() == 3);
		CHECKFOO(gCounter.totalCreated() == 4 && gCounter.constructor == 1 && gCounter.copyConstructor == 3);
		CHECKFOO(gCounter.alive() == 3);
		CHECK(cz::mut::equals(v.data(), v.size(), {2,2,2}));
	}

	SECTION("")
	{
		CREATE_DEFAULT_VECTOR(other,2);
		vector<TestType> empty;

		SECTION("Copy constructor from a non-empty vector")
		{
			gCounter.reset();
			vector<TestType> v(other);
			CHECKFOO(gCounter.totalCreated()==2 && gCounter.copyConstructor==2);
			CHECK(cz::mut::equals(v.data(), 2, {1,2}));
		}

		SECTION("Copy constructor from an empty vector")
		{
			gCounter.reset();
			vector<TestType> v(empty);
			CHECKFOO(gCounter.totalCreated()==0);
			CHECK(v.size() == 0);
			CHECK(v.capacity() == 0);
		}

		SECTION("Move constructor from a non-empty vector")
		{
			FooCounters before = gCounter;
			vector<TestType> v(std::move(other));
			CHECKFOO(before == gCounter); // move constructor should just swap the internals and not create/destroy any elements
			CHECK(cz::mut::equals(v.data(), 2, {1,2}));
		}

		SECTION("Move constructor from an empty vector")
		{
			gCounter.reset();
			vector<TestType> v(std::move(empty));
			CHECKFOO(gCounter.totalCreated()==0);
			CHECK(v.size() == 0);
			CHECK(v.capacity() == 0);
		}
	}

	SECTION("Destructor")
	{
		{
			CREATE_DEFAULT_VECTOR(v,5);
		}
		CHECKFOO(gCounter.alive()==0 && gCounter.destructor==5);
	}
}

VECTOR_TEST_CASE("Capacity api")
{
	gCounter.reset();

	SECTION("empty and size check")
	{
		vector<TestType> v;
		CHECK(v.empty()==true);
		CHECK(v.size()==0);
		CHECK(v.capacity()==0);
	}

	SECTION("not empty and size check")
	{
		CREATE_DEFAULT_VECTOR(v,4);
		CHECK(!v.empty());
		CHECK(v.size() == 4);
		CHECK(v.capacity() == 4);
	}

	SECTION("reserve")
	{
		SECTION("when empty")
		{
			vector<TestType> v;
			CHECK(v.capacity() == 0);
			v.reserve(10);
			CHECK(v.capacity() == 10);
			CHECKFOO(gCounter.totalCreated() == 0);
		}

		SECTION("when not empty needs to move the elements to new memory")
		{
			CREATE_DEFAULT_VECTOR(v,5);
			CHECK(v.capacity() == 5);
			const TestType* originalData = v.data();
			v.reserve(10);
			// make sure we moved to a new block of memory
			CHECK(originalData != v.data());

			// - 5 default constructed when the vector was created
			// - 5 move constructed when calling reserve
			// - 5 destroyed after being moved
			CHECKFOO(gCounter.alive()==5 && gCounter.defaultConstructor==5 && gCounter.moveConstructor==5 && gCounter.destructor==5);
			CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3,4,5}));
		}

		SECTION("when new capacity is <= than current size it does nothing")
		{
			CREATE_DEFAULT_VECTOR(v,5);
			const TestType* originalData = v.data();
			v.reserve(2);
			CHECK(v.capacity() == 5);
			CHECKFOO(gCounter.totalCreated()==5);
			// make sure we didn't move to a new block of memory
			CHECK(originalData == v.data());
		}

		SECTION("when new capacity is <= than current capacity, it does nothing")
		{
			CREATE_DEFAULT_VECTOR(v,2);
			CHECK(v.capacity() == 2);
			v.reserve(10);
			CHECK(v.capacity() == 10);
			// 2 default constructed + 2 move constructed from the reserve
			CHECKFOO(gCounter.totalCreated()==4);

			// Trying to set a lower capacity should NOT shrink to fit
			v.reserve(v.size());
			CHECK(v.capacity() == 10);
			CHECKFOO(gCounter.totalCreated()==4);
		}
	}

	SECTION("shrink_to_fit")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		const TestType* originalData = v.data();
		v.reserve(10);
		CHECK(v.data() != originalData && v.capacity()==10);

		// 2 default constructed + 2 move constructed from the reserve
		// 2 destroyed after being moved, due to the reserve
		CHECKFOO(gCounter.totalCreated()==4 && gCounter.destructor== 2);

		originalData = v.data();
		v.shrink_to_fit();
		// Make sure we moved to another block and set the right capacity
		CHECK(v.data() != originalData && v.capacity()==2);

		// 2 default constructed + 2 move constructed from the reserve + 2 move constructed from the shrink
		CHECKFOO(gCounter.alive()==2 && gCounter.totalCreated()==6 && gCounter.destructor==4);

		CHECK(cz::mut::equals(v.data(), v.size(), {1,2}));
	}
}

VECTOR_TEST_CASE("Element acess API")
{
	gCounter.reset();
	SECTION("operator[]")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		CHECK(v[0] == 1);
		CHECK(v[1] == 2);
	}

	SECTION("front")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		CHECK(v.front() == 1);
	}

	SECTION("back")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		CHECK(v.back() == 2);
	}
}

VECTOR_TEST_CASE("Iterators API")
{
	gCounter.reset();

	SECTION("begin/end with an empty vector")
	{
		vector<TestType> v;
		CHECK(v.begin() == v.end());
	}

	SECTION("begin")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		CHECK(*v.begin() == 1);
	}

	SECTION("end must point to one after the last element")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		CHECK(v.end() == v.begin()+v.size());
	}

	SECTION("range-based for loop")
	{
		CREATE_DEFAULT_VECTOR(v,3);
		TestType tmp[3];

		SECTION("using reference")
		{
			// iterate and copy over to array so we can check we iterated correctly
			size_t idx = 0;
			for (auto& foo : v)
			{
				tmp[idx++] = foo;
			}

			CHECK(cz::mut::equals(tmp, 3, {1,2,3}));
		}

		SECTION("using const reference")
		{
			// iterate and copy over to array so we can check we iterated correctly
			size_t idx = 0;
			for (auto& foo : ((const vector<TestType>&)v))
			{
				tmp[idx++] = foo;
			}

			CHECK(cz::mut::equals(tmp, 3, {1,2,3}));
		}

		SECTION("using rvalue")
		{
			// iterate and copy over to array so we can check we iterated correctly
			size_t idx = 0;
			for (auto&& foo : v)
			{
				tmp[idx++] = std::move(foo);
			}

			if constexpr(std::is_same_v<TestType, Foo>)
			{
				CHECK(cz::mut::equals(v.data(), 3, {0,0,0}));
			}
			else
			{
				// If not using Foo, then moving the element doesn't do anything
				CHECK(cz::mut::equals(v.data(), 3, {1,2,3}));
			}
			
			CHECK(cz::mut::equals(tmp, 3, {1,2,3}));
		}
	}
}

//
// Does a cz::vector::insert test, and compares against a similar test with std::vector, to make sure we get the same result
template<typename TestType, typename T>
void doInsertTest(cz::vector<TestType>& v, TestType* at, T&& value)
{
	size_t idx = at - v.begin();
	size_t vsize = v.size();

#if HAS_STL
	std::vector<TestType> v2;
	v2.reserve(v.capacity());
	for(auto&& f : v)
		v2.push_back(f);
	auto at2 = v2.begin() + idx;
	TestType value2((int)value);
	gCounter.reset();
	v2.insert(at2, std::forward<T>(value2));
	FooCounters v2Counter = gCounter;
#else
	assert(v.capacity()<=10);
	int v2[20];
	memcpy(v2, v.data(), v.size()*sizeof(int));
	memmove(&v2[idx+1], &v2[idx], (vsize - idx) * sizeof(int));
	v2[idx] = (int)value;
#endif
	
	gCounter.reset();
	int originalValue = (int)value;
	TestType* iter = v.insert(at, std::forward<T>(value));
	CHECK(v.size()==vsize+1);
	CHECK(*iter == originalValue );

	if constexpr (std::is_rvalue_reference_v<decltype(value)>)
	{
		// Confirm it was moved
		CHECKFOO(value==0);
	}

#if HAS_STL
	CHECK(cz::mut::equals(v.data(), v.size(), v2.data(), v2.size()));
	CHECK(gCounter == v2Counter);
#else
	CHECK(cz::mut::equals(v.data(), v.size(), v2, vsize+1));
#endif
}

VECTOR_TEST_CASE("Assignment operators")
{
	gCounter.reset();
    CREATE_DEFAULT_VECTOR(v2,3);
	
	SECTION("operator=(const T&)")
	{
		SECTION("with non-empty destination and no capacity")
		{
            CREATE_DEFAULT_VECTOR(v,1);
			v = v2;

            CHECKFOO(gCounter.defaultConstructor == 3 + 1); // v2 and v1 construction
			CHECKFOO(gCounter.destructor == 1); // Destroying v1
            CHECKFOO(gCounter.copyConstructor==3); // assignment
			CHECKFOO(gCounter.alive() == 6);
			
			CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
		}
		
		SECTION("with non-empty destination and enough capacity")
		{
            vector<TestType> v;
			v.reserve(10);
			v.emplace_back();
			v = v2;

            CHECKFOO(gCounter.defaultConstructor == 3 + 1); // v2 and v1 construction
			CHECKFOO(gCounter.assigned == 1); // v[0] = v2[0]
            CHECKFOO(gCounter.copyConstructor==2); // constructing v[1..2]
			CHECKFOO(gCounter.alive() == 6);
			
			CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
		}
		
		SECTION("with empty destination")
		{
            vector<TestType> v;
			v.reserve(10);
			v = v2;
			CHECKFOO(gCounter.destructor == 0);
            CHECKFOO(gCounter.copyConstructor==3 && gCounter.alive() == 6);
			CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
		}

		SECTION("with new size <= current size")
		{
            CREATE_DEFAULT_VECTOR(v,5);
			v = v2;
			
            CHECKFOO(gCounter.defaultConstructor == 3 + 5); // v2 and v1 construction
			CHECKFOO(gCounter.assigned == 3); // v[0..2] = v2[0..2]
            CHECKFOO(gCounter.destructor==2); // destroying v[3..4];
			CHECKFOO(gCounter.alive() == 6);
			
			CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
		}
	}

	SECTION("operator=(T&&")
	{
		CREATE_DEFAULT_VECTOR(v,1);
		gCounter.reset();
		v = std::move(v2);

		// It should be just destroying v, and stealing the contents of v2
#if USE_FOO // if using int the moving does  nothing
		FooCounters expected;
		expected.destructor = 1;
		CHECK(gCounter == expected);
#endif
		
		CHECK(v2.size() == 0);
		CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
	}
}

VECTOR_TEST_CASE("Modifiers API")
{
	gCounter.reset();

	SECTION("clear")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		v.clear();
		CHECKFOO(gCounter.alive()==0);
		CHECK(v.size() == 0);
		// Standard says it should not change the capacity
		CHECK(v.capacity() == 2);
	}

	SECTION("emplace_back")
	{
		SECTION("when starting with an empty vector")
		{
			vector<TestType> v;
			TestType& f = v.emplace_back(4);
			CHECK(v.size() == 1 && v.capacity() == 1);
			CHECKFOO(gCounter.totalCreated() == 1 && gCounter.constructor == 1);
			CHECK(v[0] == 4 && f == 4);
		}
		
		SECTION("Start with an empty vector but with sufficient capacity")
		{
			vector<TestType> v;
			v.reserve(1);
			CHECK(v.capacity()==1);
			TestType& f = v.emplace_back(4);
			CHECK(v.capacity()==1);
			CHECKFOO(gCounter.totalCreated() == 1 && gCounter.constructor == 1);
			CHECK(v[0] == 4 && f == 4);
		}

		SECTION("different types of constructors")
		{
			vector<TestType> v;
			SECTION("normal")
			{
				TestType& f = v.emplace_back(4);
				CHECK(f==4)
				CHECKFOO(gCounter.totalCreated() == 1 && gCounter.constructor == 1);
			}

			SECTION("const ref")
			{
				TestType tmp(4);
				gCounter.reset();
				TestType& f = v.emplace_back(tmp);
				CHECK(f==4);
				CHECKFOO(gCounter.totalCreated() == 1 && gCounter.copyConstructor == 1);
			}

			SECTION("rvalue ref")
			{
				TestType tmp(4);
				gCounter.reset();
				TestType& f = v.emplace_back(std::move(tmp));
				CHECKFOO(tmp == 0); // make sure we moved tmp
				CHECK(f==4);
				CHECKFOO(gCounter.totalCreated() == 1 && gCounter.moveConstructor == 1);
			}

            // if using int then this section doesn't apply
            if constexpr(std::is_same_v<TestType,Foo>)
            {
                SECTION("multiple args")
                {
                    TestType& f = v.emplace_back(5, 10);
                    CHECK(f==5);
                    CHECKFOO(gCounter.totalCreated() == 1 && gCounter.constructorExtra == 1);
                }
            }
		}
	}

	SECTION("insert")
	{
		auto testInsertImpl = [](bool doReserve, bool doMove)
		{
			// we try the insert at every possible position
			constexpr size_t vsize = 3;
			for (size_t idx = 0; idx<=vsize; idx++)
			{
				vector<TestType> v(vsize);
				if (doReserve)
				{
					v.reserve(vsize+1);
				}
				TestType f(vsize+1);
				if (doMove)
				{
					doInsertTest(v, v.begin()+idx, std::move(f));
				}
				else
				{
					doInsertTest(v, v.begin() + idx, f);
				}
			}
		};

		SECTION("with sufficient capacity")
		{
			testInsertImpl(true, false);
			testInsertImpl(true, true);
		}

		SECTION("without sufficient capacity")
		{
			testInsertImpl(false, false);
			testInsertImpl(false, true);
		}

	}

	SECTION("push_back")
	{
		// No need for complex tests, since it just uses emplace_back internally
		vector<TestType> v;
		TestType f1(2);
		v.push_back(f1);
		TestType f2(3);
		v.push_back(std::move(f2));
		CHECK(cz::mut::equals(v.data(), v.size(), {2,3}));
		CHECK(f1==2); // confirm that it took a const ref
		CHECKFOO(f2==0); // confirm that it took a rvalue ref
	}

	SECTION("pop_back")
	{
		CREATE_DEFAULT_VECTOR(v,2);
		gCounter.reset();
		v.pop_back();
		CHECKFOO(v.size()==1 && gCounter.destructor==1);
		v.pop_back();
		CHECKFOO(v.size()==0 && gCounter.destructor==2);
	}

	SECTION("erase single")
	{
		CREATE_DEFAULT_VECTOR(v,5);
		gCounter.reset();
		TestType* p;
		CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3,4,5}));
		// Remove from middle
		p = v.erase(&v[2]);
		CHECK(*p == 4);
		CHECK(cz::mut::equals(v.data(), v.size(), {1,2,4,5}));

		// remove front
		p = v.erase(&v.front());
		CHECK(*p == 2);
		CHECK(cz::mut::equals(v.data(), v.size(), {2,4,5}));

		// remove back
		// Standard says: "If pos refers to the last element, then end() iterator is returned
		p = v.erase(&v.back());
		CHECK(p == v.end());
		CHECK(cz::mut::equals(v.data(), v.size(), {2,4}));
		// Now test that if we remove the very last element, we still get an end() iterator
		v.pop_back();
		CHECK(v.size()==1);
		p = v.erase(v.begin());
		CHECK(v.size()==0);
		CHECK(p == v.end());

		CHECKFOO(gCounter.destructor == 5 && gCounter.totalCreated() == 0);
	}

	SECTION("erase range")
	{
		gCounter.reset();
		CREATE_DEFAULT_VECTOR(v,5);

		SECTION("erasing empty range should be a no-op")
		{
			FooCounters expected = gCounter;
			CHECK(v.erase(v.begin(), v.begin()) == v.begin());
			CHECK(v.erase(v.end(), v.end()) == v.end());
			CHECK(gCounter == expected);
			CHECK(v.size() == 5);
		}

		SECTION("erase all")
		{
			CHECK(v.erase(v.begin(), v.end()) == v.begin());
			CHECKFOO(gCounter.defaultConstructor == 5); // constructing v
			CHECKFOO(gCounter.destructor == 5); // destroying all elements
			CHECK(v.size() == 0);
		}

		SECTION("Erase at the beginning")
		{
			//  indexes     : 0 1 2 3 4
			// start values : 1 2 3 4 5
			// final values : 3,4,5 - -
			TestType* iter = v.erase(v.begin(), v.begin() + 2); // erase [0..1]
			CHECK(*iter == 3);
			CHECKFOO(gCounter.defaultConstructor == 5); // constructing v
			CHECKFOO(gCounter.moveAssigned == 3); // moving [2..4] over [0..2]
			CHECKFOO(gCounter.destructor == 2); // destroying [3..4]
			CHECK(cz::mut::equals(v.data(), v.size(), {3,4,5}));
		}

		SECTION("Erase at the middle")
		{
			//  indexes     : 0 1 2 3 4
			// start values : 1 2 3 4 5
			// final values : 1,4,5 - -
			TestType* iter = v.erase(&v[1], &v[3]) ; // erase [1..2]
			CHECK(*iter == 4);
			CHECKFOO(gCounter.defaultConstructor == 5); // constructing v
			CHECKFOO(gCounter.moveAssigned == 2); // moving [3..4] over [1..2]
			CHECKFOO(gCounter.destructor == 2); // destroying [3..4]
			CHECK(cz::mut::equals(v.data(), v.size(), {1,4,5}));
			
		}
		
		SECTION("Erase at the end")
		{
			//  indexes     : 0 1 2 3 4
			// start values : 1 2 3 4 5
			// final values : 1 2 3 - -
			TestType* iter = v.erase(v.end()-2, v.end()) ; // erase [3..4]
			CHECK(iter == v.end());
			CHECKFOO(gCounter.defaultConstructor == 5); // constructing v
			CHECKFOO(gCounter.moveAssigned == 0); // no assignment because it's at the end
			CHECKFOO(gCounter.destructor == 2); // destroying [3..4]
			CHECK(cz::mut::equals(v.data(), v.size(), {1,2,3}));
			
		}
	}
}

VECTOR_TEST_CASE("operators")
{
	gCounter.reset();
    CREATE_DEFAULT_VECTOR(a,3);

	gCounter.reset();
    CREATE_DEFAULT_VECTOR(b,3);

	gCounter.reset();
    CREATE_DEFAULT_VECTOR(c,2);

	SECTION("operator==")
	{
		CHECK(a == b);
		CHECK(!(a == c));
	}

	SECTION("operator!=")
	{
		CHECK(!(a != b));
		CHECK(a != c);
	}
}
