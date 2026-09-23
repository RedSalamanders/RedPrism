#pragma once

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

#include <climits>
#include <cmath>
#include <string>

// Shared yyjson reading helpers (SettingsStore, ProjectStore). `required` tells a missing key (fine unless
// required, in which case it is a parse failure) apart from one present with the wrong type (always a failure);
// every function leaves `value` exactly as ReadString documents on failure. Strings are length-aware
// (`yyjson_get_len`), never a `strlen`-style read that would stop early at an embedded NUL.
namespace JsonRead
{
using unique_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;

// `field` as a double: an int, uint or real, finite. An integral real (10.0, as some writers emit for a whole
// number) reads the same as the int 10, so ReadInt below accepts either.
[[nodiscard]] inline bool NumberValue(yyjson_val* field, double& value) noexcept
{
    if (field == nullptr)
    {
        return false;
    }
    if (yyjson_is_real(field))
    {
        value = yyjson_get_real(field);
        return std::isfinite(value);
    }
    if (yyjson_is_int(field))
    {
        value = yyjson_is_uint(field) ? static_cast<double>(yyjson_get_uint(field))
                                      : static_cast<double>(yyjson_get_sint(field));
        return true;
    }
    return false;
}

// `object[key]` as a finite double. False (value untouched) when the key is missing and `required`, or present
// with a non-numeric or non-finite value; when it is missing and not required, true with `value` untouched.
[[nodiscard]] inline bool ReadNumber(yyjson_val* object, const char* key, double& value, bool required) noexcept
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        return !required;
    }
    return NumberValue(field, value);
}

// `object[key]` as an int: an integer, or a real with no fraction, within int range. Same required/missing rule
// as ReadNumber.
[[nodiscard]] inline bool ReadInt(yyjson_val* object, const char* key, int& value, bool required) noexcept
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        return !required;
    }
    double number = 0;
    if (!NumberValue(field, number) || number < static_cast<double>(INT_MIN) || number > static_cast<double>(INT_MAX) ||
        number != std::floor(number))
    {
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

// Same required/missing rule as ReadNumber.
[[nodiscard]] inline bool ReadBool(yyjson_val* object, const char* key, bool& value, bool required) noexcept
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        return !required;
    }
    if (!yyjson_is_bool(field))
    {
        return false;
    }
    value = yyjson_get_bool(field);
    return true;
}

// `value` is cleared when the key is missing (matching a Swift `Codable` optional string's decoded default), and
// left untouched when it is present with the wrong type.
[[nodiscard]] inline bool ReadString(yyjson_val* object, const char* key, std::string& value, bool required) noexcept
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        value.clear();
        return !required;
    }
    if (!yyjson_is_str(field))
    {
        return false;
    }
    value.assign(yyjson_get_str(field), yyjson_get_len(field));
    return true;
}
} // namespace JsonRead
