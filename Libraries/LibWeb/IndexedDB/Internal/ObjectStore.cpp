/*
 * Copyright (c) 2025, stelar7 <dudedbz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/BinarySearch.h>
#include <AK/Math.h>
#include <LibWeb/IndexedDB/IDBKeyRange.h>
#include <LibWeb/IndexedDB/Internal/ObjectStore.h>

namespace Web::IndexedDB {

GC_DEFINE_ALLOCATOR(ObjectStore);

ObjectStore::~ObjectStore() = default;

GC::Ref<ObjectStore> ObjectStore::create(JS::Realm& realm, GC::Ref<Database> database, String name, bool auto_increment, Optional<KeyPath> const& key_path)
{
    return realm.create<ObjectStore>(database, name, auto_increment, key_path);
}

ObjectStore::ObjectStore(GC::Ref<Database> database, String name, bool auto_increment, Optional<KeyPath> const& key_path)
    : m_database(database)
    , m_name(move(name))
    , m_key_path(key_path)
{
    database->add_object_store(*this);

    if (auto_increment)
        m_key_generator = KeyGenerator {};
}

void ObjectStore::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_database);
    visitor.visit(m_indexes);

    for (auto& record : m_records) {
        visitor.visit(record.key);
    }
}

void ObjectStore::remove_records_in_range(GC::Ref<IDBKeyRange> range)
{
    if (m_records.is_empty())
        return;

    // Since records are sorted by key, records in range form a contiguous block.
    // Binary search for the first record in range.
    size_t lo = 0;
    size_t hi = m_records.size();

    auto lower = range->lower_key();
    if (lower) {
        // Find the first record with key >= lower (or > lower if lower_open).
        size_t l = 0, h = m_records.size();
        while (l < h) {
            size_t mid = l + (h - l) / 2;
            auto cmp = Key::compare_two_keys(m_records[mid].key, *lower);
            if (cmp < 0 || (cmp == 0 && range->lower_open()))
                l = mid + 1;
            else
                h = mid;
        }
        lo = l;
    }

    auto upper = range->upper_key();
    if (upper) {
        // Find the first record with key > upper (or >= upper if upper_open).
        size_t l = lo, h = m_records.size();
        while (l < h) {
            size_t mid = l + (h - l) / 2;
            auto cmp = Key::compare_two_keys(m_records[mid].key, *upper);
            if (cmp < 0 || (cmp == 0 && !range->upper_open()))
                l = mid + 1;
            else
                h = mid;
        }
        hi = l;
    }

    if (lo < hi)
        m_records.remove(lo, hi - lo);
}

bool ObjectStore::has_record_with_key(GC::Ref<Key> key)
{
    return binary_search(m_records, key, nullptr, [](auto const& needle, auto const& record) -> int {
        return Key::compare_two_keys(needle, record.key);
    }) != nullptr;
}

void ObjectStore::store_a_record(ObjectStoreRecord const& record)
{
    // NOTE: The record is stored in the object store’s list of records such that the list is sorted according to the key of the records in ascending order.
    //       We use binary search to find the correct insertion position.
    size_t lo = 0;
    size_t hi = m_records.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (Key::compare_two_keys(m_records[mid].key, record.key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    m_records.insert(lo, record);
}

u64 ObjectStore::count_records_in_range(GC::Ref<IDBKeyRange> range)
{
    u64 count = 0;
    for (auto const& record : m_records) {
        if (range->is_in_range(record.key))
            ++count;
    }
    return count;
}

Optional<ObjectStoreRecord&> ObjectStore::first_in_range(GC::Ref<IDBKeyRange> range)
{
    return m_records.first_matching([&](auto const& record) {
        return range->is_in_range(record.key);
    });
}

void ObjectStore::clear_records()
{
    m_records.clear();
}

GC::ConservativeVector<ObjectStoreRecord> ObjectStore::first_n_in_range(GC::Ref<IDBKeyRange> range, Optional<WebIDL::UnsignedLong> count)
{
    GC::ConservativeVector<ObjectStoreRecord> records(range->heap());
    for (auto const& record : m_records) {
        if (range->is_in_range(record.key))
            records.append(record);

        if (count.has_value() && records.size() >= *count)
            break;
    }

    return records;
}

GC::ConservativeVector<ObjectStoreRecord> ObjectStore::last_n_in_range(GC::Ref<IDBKeyRange> range, Optional<WebIDL::UnsignedLong> count)
{
    GC::ConservativeVector<ObjectStoreRecord> records(range->heap());
    for (auto const& record : m_records.in_reverse()) {
        if (range->is_in_range(record.key))
            records.append(record);

        if (count.has_value() && records.size() >= *count)
            break;
    }

    return records;
}

}
