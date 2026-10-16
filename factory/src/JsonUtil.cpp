#include "zfactory/JsonUtil.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace zfactory
{
namespace json
{

namespace
{
/** juce::DynamicObject only accepts juce::Identifier keys; our schema keys are
    fixed ASCII identifiers, so the conversion is total. */
inline juce::Identifier ident (const std::string& key)
{
    return juce::Identifier (key.c_str());
}
} // namespace

const char* varTypeName (const juce::var& value)
{
    if (value.isVoid())   return "null";
    if (value.isBool())   return "boolean";
    if (value.isInt() || value.isInt64() || value.isDouble()) return "number";
    if (value.isString()) return "string";

    // NOTE: an array var reports isObject() == true in JUCE, so the array test
    // has to come first or arrays would be described as objects.
    if (value.isArray())  return "array";
    if (value.isObject()) return "object";

    if (value.isBinaryData()) return "binary";
    return "unknown";
}

std::string formatNumber (double value)
{
    if (value == std::floor (value) && std::abs (value) < 1.0e15)
        return std::to_string (static_cast<long long> (value));

    // Fixed, locale-independent formatting with enough digits to identify the
    // value but no more (so error text stays stable across platforms).
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.9g", value);
    return std::string (buffer);
}

std::string childPath (const std::string& base, const std::string& key)
{
    return base + "/" + key;
}

std::string indexPath (const std::string& base, int index)
{
    return base + "[" + std::to_string (index) + "]";
}

std::string sortedKeyList (const juce::DynamicObject& object)
{
    std::set<std::string> keys;

    const auto& properties = object.getProperties();

    for (int i = 0; i < properties.size(); ++i)
        keys.insert (properties.getName (i).toString().toStdString());

    std::string result;

    for (const auto& key : keys)
    {
        if (! result.empty())
            result += ", ";
        result += key;
    }

    return result;
}

bool checkKeys (const juce::DynamicObject& object,
                const std::string& path,
                const std::vector<std::string>& allowed,
                FactoryError& error)
{
    std::set<std::string> unexpected;
    const auto& properties = object.getProperties();

    for (int i = 0; i < properties.size(); ++i)
    {
        const auto key = properties.getName (i).toString().toStdString();

        if (std::find (allowed.begin(), allowed.end(), key) == allowed.end())
            unexpected.insert (key);
    }

    if (unexpected.empty())
        return true;

    // Deterministic: always report the lexicographically smallest offender.
    const std::string first = *unexpected.begin();

    error = makeError (ErrorCode::UnexpectedField,
                       childPath (path, first),
                       "unexpected field (allowed: " + sortedKeyList (object) + ")");
    return false;
}

bool readRootObject (const juce::var& root, const juce::DynamicObject*& out, FactoryError& error)
{
    // getDynamicObject(), not isObject(): in JUCE an array var also reports
    // isObject() == true while getDynamicObject() returns nullptr, so only the
    // pointer check is safe.
    out = root.getDynamicObject();

    if (out == nullptr)
    {
        error = makeError (ErrorCode::RootNotObject, "$",
                           "PluginSpec root must be a JSON object, found "
                               + std::string (varTypeName (root)));
        return false;
    }

    return true;
}

bool requireField (const juce::DynamicObject& object,
                   const std::string& path,
                   const std::string& key,
                   const juce::var*& out,
                   FactoryError& error)
{
    if (! object.hasProperty (ident (key)))
    {
        error = makeError (ErrorCode::MissingRequiredField, childPath (path, key),
                           "required field is missing");
        return false;
    }

    const auto& value = object.getProperty (ident (key));

    if (value.isVoid())
    {
        error = makeError (ErrorCode::MissingRequiredField, childPath (path, key),
                           "required field is present but null");
        return false;
    }

    out = &value;
    return true;
}

bool requireString (const juce::DynamicObject& object,
                    const std::string& path,
                    const std::string& key,
                    std::string& out,
                    FactoryError& error)
{
    const juce::var* value = nullptr;

    if (! requireField (object, path, key, value, error))
        return false;

    if (! value->isString())
    {
        error = makeError (ErrorCode::TypeMismatch, childPath (path, key),
                           "expected a JSON string, found " + std::string (varTypeName (*value)));
        return false;
    }

    out = value->toString().toStdString();
    return true;
}

bool requireNumber (const juce::DynamicObject& object,
                    const std::string& path,
                    const std::string& key,
                    double& out,
                    FactoryError& error)
{
    const juce::var* value = nullptr;

    if (! requireField (object, path, key, value, error))
        return false;

    return elementAsNumber (*value, childPath (path, key), out, error);
}

bool requireInt (const juce::DynamicObject& object,
                 const std::string& path,
                 const std::string& key,
                 int& out,
                 FactoryError& error)
{
    double number = 0.0;

    if (! requireNumber (object, path, key, number, error))
        return false;

    if (number != std::floor (number))
    {
        error = makeError (ErrorCode::TypeMismatch, childPath (path, key),
                           "expected an integer, found " + formatNumber (number));
        return false;
    }

    if (number < -2147483647.0 || number > 2147483647.0)
    {
        error = makeError (ErrorCode::ValueOutOfRange, childPath (path, key),
                           "integer out of range: " + formatNumber (number));
        return false;
    }

    out = static_cast<int> (number);
    return true;
}

bool requireBool (const juce::DynamicObject& object,
                  const std::string& path,
                  const std::string& key,
                  bool& out,
                  FactoryError& error)
{
    const juce::var* value = nullptr;

    if (! requireField (object, path, key, value, error))
        return false;

    if (! value->isBool())
    {
        error = makeError (ErrorCode::TypeMismatch, childPath (path, key),
                           "expected a JSON boolean, found " + std::string (varTypeName (*value)));
        return false;
    }

    out = static_cast<bool> (*value);
    return true;
}

bool requireObject (const juce::DynamicObject& object,
                    const std::string& path,
                    const std::string& key,
                    const juce::DynamicObject*& out,
                    FactoryError& error)
{
    const juce::var* value = nullptr;

    if (! requireField (object, path, key, value, error))
        return false;

    return elementAsObject (*value, childPath (path, key), out, error);
}

bool requireArray (const juce::DynamicObject& object,
                   const std::string& path,
                   const std::string& key,
                   const juce::Array<juce::var>*& out,
                   FactoryError& error)
{
    const juce::var* value = nullptr;

    if (! requireField (object, path, key, value, error))
        return false;

    if (! value->isArray())
    {
        error = makeError (ErrorCode::TypeMismatch, childPath (path, key),
                           "expected a JSON array, found " + std::string (varTypeName (*value)));
        return false;
    }

    out = value->getArray();
    return true;
}

bool optionalString (const juce::DynamicObject& object,
                     const std::string& path,
                     const std::string& key,
                     std::string& out,
                     FactoryError& error)
{
    if (! object.hasProperty (ident (key)))
        return true;

    const auto& value = object.getProperty (ident (key));

    if (value.isVoid())
        return true;

    if (! value.isString())
    {
        error = makeError (ErrorCode::TypeMismatch, childPath (path, key),
                           "expected a JSON string, found " + std::string (varTypeName (value)));
        return false;
    }

    out = value.toString().toStdString();
    return true;
}

bool optionalNumber (const juce::DynamicObject& object,
                     const std::string& path,
                     const std::string& key,
                     double& out,
                     FactoryError& error)
{
    if (! object.hasProperty (ident (key)))
        return true;

    const auto& value = object.getProperty (ident (key));

    if (value.isVoid())
        return true;

    return elementAsNumber (value, childPath (path, key), out, error);
}

bool optionalBool (const juce::DynamicObject& object,
                   const std::string& path,
                   const std::string& key,
                   bool& out,
                   FactoryError& error)
{
    if (! object.hasProperty (ident (key)))
        return true;

    const auto& value = object.getProperty (ident (key));

    if (value.isVoid())
        return true;

    if (! value.isBool())
    {
        error = makeError (ErrorCode::TypeMismatch, childPath (path, key),
                           "expected a JSON boolean, found " + std::string (varTypeName (value)));
        return false;
    }

    out = static_cast<bool> (value);
    return true;
}

bool elementAsString (const juce::var& element, const std::string& path, std::string& out, FactoryError& error)
{
    if (! element.isString())
    {
        error = makeError (ErrorCode::TypeMismatch, path,
                           "expected a JSON string, found " + std::string (varTypeName (element)));
        return false;
    }

    out = element.toString().toStdString();
    return true;
}

bool elementAsObject (const juce::var& element, const std::string& path, const juce::DynamicObject*& out, FactoryError& error)
{
    // See readRootObject(): the pointer check is the only reliable object test.
    out = element.getDynamicObject();

    if (out == nullptr)
    {
        error = makeError (ErrorCode::TypeMismatch, path,
                           "expected a JSON object, found " + std::string (varTypeName (element)));
        return false;
    }

    return true;
}

bool elementAsNumber (const juce::var& element, const std::string& path, double& out, FactoryError& error)
{
    if (! (element.isInt() || element.isInt64() || element.isDouble()) || element.isBool())
    {
        error = makeError (ErrorCode::TypeMismatch, path,
                           "expected a JSON number, found " + std::string (varTypeName (element)));
        return false;
    }

    const double value = static_cast<double> (element);

    if (! std::isfinite (value))
    {
        error = makeError (ErrorCode::ValueOutOfRange, path,
                           "number is not finite");
        return false;
    }

    out = value;
    return true;
}

bool hasTrailingContent (const std::string& text)
{
    const auto isSpace = [] (char c)
    {
        return std::isspace (static_cast<unsigned char> (c)) != 0;
    };

    size_t index = 0;

    while (index < text.size() && isSpace (text[index]))
        ++index;

    if (index >= text.size() || (text[index] != '{' && text[index] != '['))
        return false;   // not a container: the parser reports the real problem

    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (; index < text.size(); ++index)
    {
        const char c = text[index];

        if (inString)
        {
            if (escaped)          escaped = false;
            else if (c == '\\')   escaped = true;
            else if (c == '"')    inString = false;

            continue;
        }

        if (c == '"')         { inString = true; continue; }
        if (c == '{' || c == '[') { ++depth; continue; }

        if (c == '}' || c == ']')
        {
            if (--depth <= 0)
            {
                for (size_t rest = index + 1; rest < text.size(); ++rest)
                    if (! isSpace (text[rest]))
                        return true;

                return false;
            }
        }
    }

    return false;   // unbalanced: the parser will have failed already
}

bool isValidIdentifier (const std::string& text){
    if (text.empty() || text.size() > 64)
        return false;

    const auto isStart = [] (char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };

    const auto isRest = [&isStart] (char c)
    {
        return isStart (c) || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    };

    if (! isStart (text[0]))
        return false;

    for (size_t i = 1; i < text.size(); ++i)
        if (! isRest (text[i]))
            return false;

    return true;
}

} // namespace json
} // namespace zfactory
