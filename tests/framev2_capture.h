// FrameV2 capture infrastructure for testing.
//
// Replaces the no-op FrameV2 stubs with implementations that store
// key/value pairs, allowing tests to verify FrameV2 output fields
// (severity, signed data, error booleans, frame_number, etc.).

#pragma once

#include <map>
#include <string>
#include <vector>

#include "LogicPublicTypes.h"

// ---------------------------------------------------------------------------
// Captured field value (tagged union)
// ---------------------------------------------------------------------------

struct FrameV2FieldValue
{
    enum Type { NONE, INTEGER, STRING, BOOLEAN, DOUBLE, BYTE, BYTE_ARRAY };
    Type type;
    S64 int_val;
    std::string str_val;
    bool bool_val;
    double dbl_val;
    U8 byte_val;
    std::vector<U8> byte_array_val;

    FrameV2FieldValue() : type( NONE ), int_val( 0 ), bool_val( false ), dbl_val( 0 ), byte_val( 0 ) {}
};

// ---------------------------------------------------------------------------
// Captured FrameV2 record
// ---------------------------------------------------------------------------

struct CapturedFrameV2
{
    std::string type;           // "slot", "advisory", etc.
    U64 starting_sample;
    U64 ending_sample;
    std::map<std::string, FrameV2FieldValue> fields;

    bool HasField( const std::string& key ) const
    {
        return fields.count( key ) > 0;
    }

    S64 GetInteger( const std::string& key ) const
    {
        auto it = fields.find( key );
        if( it != fields.end() && it->second.type == FrameV2FieldValue::INTEGER )
            return it->second.int_val;
        return 0;
    }

    std::string GetString( const std::string& key ) const
    {
        auto it = fields.find( key );
        if( it != fields.end() && it->second.type == FrameV2FieldValue::STRING )
            return it->second.str_val;
        return "";
    }

    bool GetBoolean( const std::string& key ) const
    {
        auto it = fields.find( key );
        if( it != fields.end() && it->second.type == FrameV2FieldValue::BOOLEAN )
            return it->second.bool_val;
        return false;
    }

    std::vector<U8> GetByteArray( const std::string& key ) const
    {
        auto it = fields.find( key );
        if( it != fields.end() && it->second.type == FrameV2FieldValue::BYTE_ARRAY )
            return it->second.byte_array_val;
        return {};
    }
};

// ---------------------------------------------------------------------------
// Global capture API
// ---------------------------------------------------------------------------

// Returns the vector of all captured FrameV2 records since last clear.
std::vector<CapturedFrameV2>& GetCapturedFrameV2s();

// Clears the capture buffer. Call before each test that inspects FrameV2.
void ClearCapturedFrameV2s();
