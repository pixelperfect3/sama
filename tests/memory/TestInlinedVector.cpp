#include <catch2/catch_test_macros.hpp>
#include <string>

#include "engine/memory/InlinedVector.h"

using engine::memory::InlinedVector;

TEST_CASE("InlinedVector default construction", "[memory]")
{
    InlinedVector<int, 8> v;
    REQUIRE(v.size() == 0);
    REQUIRE(v.capacity() == 8);
    REQUIRE(v.empty());
}

TEST_CASE("InlinedVector push_back up to N stays inline", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    v.push_back(40);
    REQUIRE(v.size() == 4);
    REQUIRE(v.capacity() == 4);
    REQUIRE(v[0] == 10);
    REQUIRE(v[1] == 20);
    REQUIRE(v[2] == 30);
    REQUIRE(v[3] == 40);
}

TEST_CASE("InlinedVector push_back beyond N triggers heap fallback", "[memory]")
{
    InlinedVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    REQUIRE(v.capacity() == 2);

    v.push_back(3);
    REQUIRE(v.size() == 3);
    REQUIRE(v.capacity() > 2);
    // All elements preserved after heap fallback.
    REQUIRE(v[0] == 1);
    REQUIRE(v[1] == 2);
    REQUIRE(v[2] == 3);
}

TEST_CASE("InlinedVector pop_back", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.pop_back();
    REQUIRE(v.size() == 2);
    REQUIRE(v.back() == 2);
}

TEST_CASE("InlinedVector operator[] and at()", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    REQUIRE(v[0] == 10);
    REQUIRE(v[1] == 20);
    REQUIRE(v[2] == 30);

    REQUIRE(v.at(0) == 10);
    REQUIRE(v.at(1) == 20);
    REQUIRE(v.at(2) == 30);

    REQUIRE_THROWS_AS(v.at(3), std::out_of_range);
}

TEST_CASE("InlinedVector range-for iteration", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    int sum = 0;
    for (int x : v)
    {
        sum += x;
    }
    REQUIRE(sum == 6);
}

TEST_CASE("InlinedVector clear resets size", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.clear();
    REQUIRE(v.size() == 0);
    REQUIRE(v.empty());
}

TEST_CASE("InlinedVector resize larger and smaller", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(10);

    v.resize(3);
    REQUIRE(v.size() == 3);
    REQUIRE(v[0] == 10);
    REQUIRE(v[1] == 0);
    REQUIRE(v[2] == 0);

    v.resize(1);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0] == 10);
}

TEST_CASE("InlinedVector reserve beyond N", "[memory]")
{
    InlinedVector<int, 2> v;
    v.push_back(5);
    v.reserve(16);
    REQUIRE(v.capacity() >= 16);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0] == 5);
}

TEST_CASE("InlinedVector copy construction", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    InlinedVector<int, 4> copy(v);
    REQUIRE(copy.size() == 3);
    REQUIRE(copy[0] == 1);
    REQUIRE(copy[1] == 2);
    REQUIRE(copy[2] == 3);

    // Modifying copy doesn't affect original.
    copy[0] = 99;
    REQUIRE(v[0] == 1);
}

TEST_CASE("InlinedVector move construction", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);

    InlinedVector<int, 4> moved(std::move(v));
    REQUIRE(moved.size() == 2);
    REQUIRE(moved[0] == 10);
    REQUIRE(moved[1] == 20);
    REQUIRE(v.size() == 0);
}

TEST_CASE("InlinedVector copy assignment", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);

    InlinedVector<int, 4> other;
    other.push_back(99);
    other = v;

    REQUIRE(other.size() == 2);
    REQUIRE(other[0] == 1);
    REQUIRE(other[1] == 2);

    // Self-assignment.
    other = other;
    REQUIRE(other.size() == 2);
}

TEST_CASE("InlinedVector move assignment", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);

    InlinedVector<int, 4> other;
    other.push_back(99);
    other = std::move(v);

    REQUIRE(other.size() == 2);
    REQUIRE(other[0] == 10);
    REQUIRE(other[1] == 20);
    REQUIRE(v.size() == 0);
}

TEST_CASE("InlinedVector emplace_back", "[memory]")
{
    InlinedVector<std::string, 4> v;
    v.emplace_back("hello");
    v.emplace_back(3, 'x');

    REQUIRE(v.size() == 2);
    REQUIRE(v[0] == "hello");
    REQUIRE(v[1] == "xxx");
}

TEST_CASE("InlinedVector erase single element", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);

    auto it = v.erase(v.begin() + 1);  // erase element '2'
    REQUIRE(v.size() == 3);
    REQUIRE(v[0] == 1);
    REQUIRE(v[1] == 3);
    REQUIRE(v[2] == 4);
    REQUIRE(*it == 3);
}

TEST_CASE("InlinedVector front() and back()", "[memory]")
{
    InlinedVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    REQUIRE(v.front() == 10);
    REQUIRE(v.back() == 30);

    v.front() = 99;
    REQUIRE(v[0] == 99);

    v.back() = 77;
    REQUIRE(v[2] == 77);
}

TEST_CASE("InlinedVector with std::string verifies non-trivial destructors", "[memory]")
{
    // This test verifies that non-trivial types are properly constructed and
    // destroyed without memory leaks or crashes.
    {
        InlinedVector<std::string, 4> v;
        v.push_back("alpha");
        v.push_back("beta");
        v.push_back("gamma");
        v.push_back("delta");
        // Push beyond inline capacity.
        v.push_back("epsilon");

        REQUIRE(v.size() == 5);
        REQUIRE(v[0] == "alpha");
        REQUIRE(v[4] == "epsilon");

        v.pop_back();
        REQUIRE(v.size() == 4);

        v.clear();
        REQUIRE(v.size() == 0);
    }
    // If destructors aren't called, sanitizers / valgrind would catch it.

    {
        InlinedVector<std::string, 4> a;
        a.push_back("one");
        a.push_back("two");

        InlinedVector<std::string, 4> b(a);
        REQUIRE(b[0] == "one");
        REQUIRE(b[1] == "two");

        InlinedVector<std::string, 4> c(std::move(a));
        REQUIRE(c[0] == "one");
        REQUIRE(c[1] == "two");
        REQUIRE(a.size() == 0);
    }
}

TEST_CASE("InlinedVector<T, 1> edge case", "[memory]")
{
    InlinedVector<int, 1> v;
    REQUIRE(v.capacity() == 1);

    v.push_back(42);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0] == 42);
    REQUIRE(v.capacity() == 1);

    // Goes to heap.
    v.push_back(99);
    REQUIRE(v.size() == 2);
    REQUIRE(v[0] == 42);
    REQUIRE(v[1] == 99);
    REQUIRE(v.capacity() > 1);
}

TEST_CASE("InlinedVector empty vector operations don't crash", "[memory]")
{
    InlinedVector<int, 4> v;
    REQUIRE(v.empty());
    REQUIRE(v.begin() == v.end());

    // clear on empty is fine.
    v.clear();
    REQUIRE(v.size() == 0);

    // resize from 0.
    v.resize(0);
    REQUIRE(v.size() == 0);

    // reserve on empty.
    v.reserve(8);
    REQUIRE(v.capacity() >= 8);
}
