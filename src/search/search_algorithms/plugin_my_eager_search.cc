#include "my_eager_search.h"
#include "search_common.h"

#include "../plugins/plugin.h"

using namespace std;

namespace plugin_my_eager_search {

class MyEagerSearchFeature
    : public plugins::TypedFeature<
          SearchAlgorithm,
          my_eager_search::My_EagerSearch> {
public:
    MyEagerSearchFeature()
        : TypedFeature("my_eager") {
        document_title("My Eager best-first search");
        document_synopsis(
            "Custom adaptation of eager search.");

        add_option<shared_ptr<OpenListFactory>>(
            "open",
            "open list");

        add_option<bool>(
            "reopen_closed",
            "reopen closed nodes",
            "false");

        add_option<shared_ptr<Evaluator>>(
            "f_eval",
            "set evaluator for jump statistics",
            plugins::ArgumentInfo::NO_DEFAULT);

        add_list_option<shared_ptr<Evaluator>>(
            "preferred",
            "use preferred operators of these evaluators",
            "[]");

        // übernimmt Standard-Search-Optionen
        my_eager_search::add_eager_search_options_to_feature(
            *this,
            "my_eager");
    }

    virtual shared_ptr<my_eager_search::My_EagerSearch>
    create_component(const plugins::Options &opts) const override {

        return plugins::make_shared_from_arg_tuples<
            my_eager_search::My_EagerSearch>(
            opts.get<shared_ptr<OpenListFactory>>("open"),
            opts.get<bool>("reopen_closed"),
            opts.get<shared_ptr<Evaluator>>("f_eval", nullptr),
            opts.get_list<shared_ptr<Evaluator>>("preferred"),
            my_eager_search::
                get_eager_search_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<MyEagerSearchFeature> _plugin;

}