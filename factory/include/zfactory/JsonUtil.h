#pragma once

#include "zfactory/Error.h"
#include "zfactory/Version.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace zfactory
{
namespace json
{

//==============================================================================
// Strict, deterministic readers over juce::var JSON trees.
//
// Every reader validates presence AND type AND (where relevant) finiteness.
// Unreadable input produces a FactoryError whose path points at the exact
// offending node. Readers never accept a JSON number where a string is
// required (or vice versa) - ambiguity is a hard error, not a coercion.
//==============================================================================

/** Human readable name of a var's JSON type, for deterministic error text. */
const char* varTypeName (const juce::var& value);

/** Deterministic, locale-independent rendering of a number for error text. */
std::string formatNumber (double value);

/** "$" -> "$/plugin"; "$/graph" -> "$/graph/modules" */
std::string childPath (const std::string& base, const std::string& key);

/** "$/graph/modules" + 2 -> "$/graph/modules[2]" */
std::string indexPath (const std::string& base, int index);

/** Sorted, comma separated list of the object's keys - used in error text so
    that the message does not depend on document key order. */
std::string sortedKeyList (const juce::DynamicObject& object);

/** Rejects any key not in `allowed`. Reports the lexicographically smallest
    offender so the error is stable regardless of JSON key order. */
bool checkKeys (const juce::DynamicObject& object,
                const std::string& path,
                const std::vector<std::string>& allowed,
                FactoryError& error);

bool readRootObject (const juce::var& root, const juce::DynamicObject*& out, FactoryError& error);

bool requireField (const juce::DynamicObject& object,
                   const std::string& path,
                   const std::string& key,
                   const juce::var*& out,
                   FactoryError& error);

bool requireString (const juce::DynamicObject& object,
                    const std::string& path,
                    const std::string& key,
                    std::string& out,
                    FactoryError& error);

bool requireNumber (const juce::DynamicObject& object,
                    const std::string& path,
                    const std::string& key,
                    double& out,
                    FactoryError& error);

bool requireInt (const juce::DynamicObject& object,
                 const std::string& path,
                 const std::string& key,
                 int& out,
                 FactoryError& error);

bool requireBool (const juce::DynamicObject& object,
                  const std::string& path,
                  const std::string& key,
                  bool& out,
                  FactoryError& error);

bool requireObject (const juce::DynamicObject& object,
                    const std::string& path,
                    const std::string& key,
                    const juce::DynamicObject*& out,
                    FactoryError& error);

bool requireArray (const juce::DynamicObject& object,
                   const std::string& path,
                   const std::string& key,
                   const juce::Array<juce::var>*& out,
                   FactoryError& error);

bool optionalString (const juce::DynamicObject& object,
                     const std::string& path,
                     const std::string& key,
                     std::string& out,
                     FactoryError& error);

bool optionalNumber (const juce::DynamicObject& object,
                     const std::string& path,
                     const std::string& key,
                     double& out,
                     FactoryError& error);

bool optionalBool (const juce::DynamicObject& object,
                   const std::string& path,
                   const std::string& key,
                   bool& out,
                   FactoryError& error);

/** Array element readers (the element node, not the array). */
bool elementAsString (const juce::var& element, const std::string& path, std::string& out, FactoryError& error);
bool elementAsObject (const juce::var& element, const std::string& path, const juce::DynamicObject*& out, FactoryError& error);
bool elementAsNumber (const juce::var& element, const std::string& path, double& out, FactoryError& error);

/** True when the document holds a complete object/array value followed by
    anything other than whitespace.

    juce::JSON::parse() silently ignores trailing bytes, which would let a
    truncated or concatenated document through as if it were valid. The factory
    loader rejects those explicitly. */
bool hasTrailingContent (const std::string& text);

/** Identifier grammar: first character a letter, then letters, digits, '.',
    '_' or '-'; 1..64 characters; ASCII only. */
bool isValidIdentifier (const std::string& text);

} // namespace json
} // namespace zfactory
