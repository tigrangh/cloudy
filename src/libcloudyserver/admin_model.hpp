#pragma once
#include "global.hpp"

//  for now workaround optional support like this
//  later try to make it more or less as it should
#include <boost/optional.hpp>
#include <belt.pp/json.hpp>
#include <belt.pp/message_global.hpp>

namespace AdminModel
{
using boost::optional;
}

namespace AdminModel
{
namespace detail
{
template <typename T>
inline
bool analyze_json(optional<T>& value,
                  ::beltpp::json::expression_tree* pexp,
                  ::beltpp::message_loader_utility const& utl);
template <typename T>
inline
std::string saver(optional<T> const& value);
}
}

#include "admin_model.gen.hpp"

namespace AdminModel
{
namespace detail
{

template <typename T>
inline
bool analyze_json(optional<T>& value,
                  ::beltpp::json::expression_tree* pexp,
                  ::beltpp::message_loader_utility const& utl)
{
    value.reset();
    bool code = true;
    if (nullptr == pexp)
        code = false;
    else
    {
        T optional_value;
        if (analyze_json(optional_value, pexp, utl))
            value = std::move(optional_value);
        else
        {
            code = false;
        }
    }

    return code;
}
template <typename T>
std::string saver(optional<T> const& value)
{
    if (value)
        return saver(*value);
    return std::string();
}
}
}
