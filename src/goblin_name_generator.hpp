//
// Created by Richard Hodges on 08/07/2017.
//

#pragma once

class goblin_name_generator {
    struct impl {
        auto generate() {
            auto result = names[pos];
            if (iteration) {
                result += " " + std::to_string(iteration);
            }
            if (++pos >= names.size()) {
                iteration++;
                pos = 0;
            }
            return result;
        }

        int iteration = 0;
        int pos = 0;
        std::array<std::string, 3> names = {"yarr!", "gnurgghhh!", "fgumschak!"};
    };

    static impl &get_impl() {
        static impl _{};
        return _;
    }

public:
    std::string operator()() const {
        return get_impl().generate();
    }
};

