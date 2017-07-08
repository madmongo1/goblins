//
// Created by Richard Hodges on 08/07/2017.
//

#include "config.hpp"
#include <boost/type_traits/integral_constant.hpp>

#pragma once


enum class goblin_error {
    actually_dead = 1,
};


struct goblin_category_def : boost::system::error_category {
    std::string message(int ev) const override {
        switch (static_cast<goblin_error>(ev)) {
            case goblin_error::actually_dead:
                return "this goblin is actually dead";
        }
    }

    const char *name() const noexcept override {
        return "goblin_category";
    }

};

inline auto goblin_category() -> goblin_category_def const & {
    static const goblin_category_def def_{};
    return def_;
}

inline boost::system::error_code make_error_code(goblin_error e)
{
    return boost::system::error_code(static_cast<int>(e), goblin_category());
}

namespace boost {
    namespace system {
        template<> struct is_error_code_enum<goblin_error> : boost::true_type {};
    }
}

