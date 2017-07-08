#pragma once

#include "config.hpp"
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/common_states.hpp>
#include <boost/optional.hpp>
#include <iostream>

namespace msm = boost::msm;
namespace msmf = boost::msm::front;
namespace msmb = boost::msm::back;


struct goblin_impl;

struct GoblinBorn {
    goblin_impl &impl;

};

struct GoblinKilledSomeone {
    goblin_impl &impl;

};

struct EventAddBirthHandler {
    std::function<void()> handler_function;
};


struct goblin_state_ : msmf::state_machine_def<goblin_state_> {
    struct Unborn : msmf::state<> {
        using birth_signal = std::function<void()>;
        std::vector<birth_signal> birth_signals;

        template<class Event, class FSM>
        void on_entry(Event const &event, FSM &fsm) {
            std::cout << "entering Unborn" << std::endl;
        };

        template<class Event, class FSM>
        void on_exit(Event const &event, FSM &fsm) {
            std::cout << "leaving Unborn" << std::endl;
            for (auto &sig : birth_signals) {
                sig();
            }
            birth_signals.clear();
        };
    };

    struct KillingFolk : msmf::state<> {
        boost::optional<asio::deadline_timer> kill_timer_;

        template<class Event, class FSM>
        void on_entry(Event const &, FSM &) {

        }

        template<class FSM>
        void on_entry(GoblinBorn const &event, FSM &fsm);

        template<class Event, class FSM>
        void on_exit(Event const &, FSM &) {
            std::cout << "destroying timer" << std::endl;
            kill_timer_.reset();
        }

    };

    struct Dead : msmf::state<> {
    };

    struct ActionAddBirthHandler {
        template<class EVT, class FSM, class SourceState, class TargetState>
        void operator()(EVT const &event, FSM &fsm, SourceState &source, TargetState &target) {

        }

        template<class FSM>
        void operator()(EventAddBirthHandler const &event, FSM &fsm, Unborn &source, Unborn &target) {
            std::cout << "born" << std::endl;
            target.birth_signals.push_back(std::move(event.handler_function));
        }

    };


    using initial_state = Unborn;

    struct transition_table : boost::mpl::vector<
            msmf::Row<Unborn, EventAddBirthHandler, msmf::none, ActionAddBirthHandler>,
            msmf::Row<Unborn, GoblinBorn, KillingFolk, msmf::none>,
            msmf::Row<KillingFolk, EventAddBirthHandler, msmf::none, ActionAddBirthHandler>
    > {
    };

    // Default no-transition handler. Can be replaced in the Derived SM class.
    template<class FSM, class Event>
    void no_transition(Event const & e, FSM &, int n) {
        std::cerr << "no transition state = " << n << " for " << typeid(e).name() << std::endl;
    }

    // default exception handler. Can be replaced in the Derived SM class.
    template<class FSM, class Event>
    void exception_caught(Event const &ev, FSM &, std::exception &e) {
        std::cerr << "exception caught = " << e.what() << " for " << typeid(ev).name() << std::endl;
    }

};

using GoblinState = msmb::state_machine<goblin_state_>;
