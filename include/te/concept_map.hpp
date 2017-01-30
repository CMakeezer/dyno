// Copyright Louis Dionne 2017
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef TE_CONCEPT_MAP_HPP
#define TE_CONCEPT_MAP_HPP

#include <te/detail/erase_function.hpp>

#include <boost/hana/at_key.hpp>
#include <boost/hana/bool.hpp>
#include <boost/hana/contains.hpp>
#include <boost/hana/is_subset.hpp>
#include <boost/hana/fold_left.hpp>
#include <boost/hana/insert.hpp>
#include <boost/hana/keys.hpp>
#include <boost/hana/map.hpp>
#include <boost/hana/pair.hpp>
#include <boost/hana/unpack.hpp>


namespace te {

// A concept map is a statically-known mapping from functions implemented by
// a type `T` to functions defined by a concept. A concept map is what's being
// used to fill the vtable of the concept it represents. An instance of this
// type is never created as-is; `te::make_concept_map` should always be used.
//
// Note that everything in the concept map is known statically. Specifically,
// the types of the functions in the concept map are known statically, and
// e.g. lambdas will be stored as-is (not as function pointers). To retrieve
// a function with an erased representation instead, use the `erased` method.
template <typename Concept, typename T, typename ...Mappings>
struct concept_map_t;

template <typename Concept, typename T, typename ...Name, typename ...Function>
struct concept_map_t<Concept, T, boost::hana::pair<Name, Function>...> {
  constexpr explicit concept_map_t(boost::hana::pair<Name, Function> ...mappings)
    : map_{mappings...}
  { }

  template <typename Name_>
  constexpr auto operator[](Name_ name) const {
    return get_function(name, boost::hana::contains(map_, name));
  }

  template <typename Name_>
  constexpr auto erased(Name_ name) const {
    auto function = (*this)[name];
    using Signature = typename decltype(Concept{}.get_signature(name))::type;
    return detail::erase_function<Signature>(function);
  }

  using concept_type = Concept;
  using model_type = T;

public: // TODO: Make this private
  boost::hana::map<boost::hana::pair<Name, Function>...> map_;

private:
  template <typename Name_>
  constexpr auto get_function(Name_ name, boost::hana::true_) const {
    return boost::hana::at_key(map_, name);
  }

  template <typename Name_>
  constexpr auto get_function(Name_ name, boost::hana::false_) const {
    constexpr bool always_false = sizeof(Name_) == 0;
    static_assert(always_false,
      "te::concept_map_t::operator[]: Request for the implementation of a "
      "function that was not provided in the concept map. Make sure the "
      "concept map contains the proper functions, and that you're requesting "
      "the right function from the concept map. You can find the contents of "
      "the concept map and the function you were trying to access in the "
      "compiler error message, probably in the following format: "
      "`concept_map_t<CONCEPT, MODEL, CONTENTS OF CONCEPT MAP>::get_function<FUNCTION NAME>`");
  }
};

template <typename Concept, typename T, typename ...Name, typename ...Function>
constexpr auto make_default_concept_map(boost::hana::pair<Name, Function> ...);

template <typename Concept, typename T, typename ...Name, typename ...Function>
constexpr auto make_concept_map(boost::hana::pair<Name, Function> ...);

// Customization point for concept writers to provide default models of
// their concepts.
//
// This can be specialized by concept authors to provide a concept map that
// will be used when no custom concept map is specified. The third parameter
// can be used to define a default concept map for a family of type, by using
// `std::enable_if`.
//
// Note that unlike `te::concept_map`, which should be populated
// using `te::make_concept_map`, this must be populated using
// `te::make_default_concept_map`.
template <typename Concept, typename T, typename = void>
auto const default_concept_map = te::make_default_concept_map<Concept, T>();

// Customization point for users to define their models of concepts.
//
// This can be specialized by clients to provide concept maps for the concepts
// and types they wish. The third parameter can be used to define a concept
// map for a family of type, by using `std::enable_if`.
//
// Example usage:
// ```
// namespace my {
//   struct Drawable : decltype(te::requires(
//     "draw"_s = te::function<void (std::ostream&, te::T const&)>
//   )) { };
// }
//
// struct Foo { ... };
//
// template <>
// auto const te::concept_map<my::Drawable, Foo> = te::make_concept_map<my::Drawable, Foo>(
//   "draw"_s = [](std::ostream& os, Foo const& foo) { ... }
// );
// ```
template <typename Concept, typename T, typename = void>
auto const concept_map = te::make_concept_map<Concept, T>();

namespace detail {
  // TODO: This should be `boost::hana::union_` for maps, I think.
  template <typename Map1, typename Map2>
  constexpr auto merge(Map1 map1, Map2 map2) {
    return boost::hana::fold_left(map2, map1, boost::hana::insert);
  }
} // end namespace detail

// Creates a concept map for how the type `T` models the given `Concept`.
//
// This is achieved by providing a mapping from function names (as compile-time
// strings) to function implementations (as stateless function objects).
//
// Note that the concept maps for all the concepts that `Concept` refines are
// merged with the mappings provided explicitly. For example:
// ```
// struct A : decltype(te::requires(
//   "f"_s = te::function<void (te::T&)>
// )) { };
//
// struct B : decltype(te::requires(
//   A{},
//   "g"_s = te::function<int (te::T&)>
// )) { };
//
// struct Foo { };
//
// template <>
// auto const te::concept_map<A, Foo> = te::make_concept_map<A, Foo>(
//   "f"_s = [](Foo&) { }
// );
//
// template <>
// auto const te::concept_map<B, Foo> = te::make_concept_map<B, Foo>(
//   "g"_s = [](Foo&) { return 0; }
//   // `f` is automatically pulled from the concept map for `A`
// );
// ```
//
// Furthermore, if the same function is defined in more than one concept map
// in the full refinement tree, it is undefined which one is used. Therefore,
// all of the implementations better be the same! This is easy to enforce by
// never defining a function in a concept map where the concept does not
// require that function.
//
// Despite the above, which relates to functions defined in different concepts
// that have a refinement relation, if a function is provided both in the
// default concept map and in a customized concept map for the same concept,
// the function in the customized map is preferred.
//
// Also, after looking at the whole refinement tree, including the default
// concept maps, it is an error if any function required by the concept can't
// be resolved.
template <typename Concept, typename T, typename ...Name, typename ...Function>
constexpr auto make_concept_map(boost::hana::pair<Name, Function> ...mappings) {
  auto mappings_ = detail::merge(boost::hana::make_map(mappings...),
                                 te::default_concept_map<Concept, T>.map_);
  auto refined = Concept::refined_concepts();
  auto merged = boost::hana::fold_left(refined, mappings_, [](auto mappings, auto c) {
    using C = typename decltype(c)::type;
    return detail::merge(mappings, te::concept_map<C, T>.map_);
  });
  constexpr bool all_functions_satisfied = decltype(boost::hana::is_subset(
    boost::hana::keys(Concept::all_clauses()),
    boost::hana::keys(merged)
  )){};
  static_assert(all_functions_satisfied,
    "te::make_concept_map: Incomplete definition of the concept map. Despite "
    "looking at the default concept map for this concept and the concept maps "
    "for all the concepts this concept refines, I can't find definitions for "
    "all the functions that the concept requires. Please make sure you did not "
    "forget to define a function in your concept map, and otherwise make sure "
    "the proper default concept maps are kicking in.");
  return boost::hana::unpack(merged, [](auto ...m) {
    return te::concept_map_t<Concept, T, decltype(m)...>{m...};
  });
}

// Equivalent to `te::make_concept_map`, but for populating default concept maps.
//
// TODO:
// This is a not-so-great solution for the following problem: when populating
// a default concept map, we want to be able to populate it only partially,
// while leaving the rest of the map to be populated by an explicit customization.
// We can't use `te::make_concept_map` for this, because it makes sure that
// the created concept map is sufficient to fulfill the concept, which would
// not be the case for a default concept map.
template <typename Concept, typename T, typename ...Name, typename ...Function>
constexpr auto make_default_concept_map(boost::hana::pair<Name, Function> ...mappings) {
  auto mappings_ = boost::hana::make_map(mappings...);
  auto refined = Concept::refined_concepts();
  auto merged = boost::hana::fold_left(refined, mappings_, [](auto mappings, auto c) {
    using C = typename decltype(c)::type;
    return detail::merge(mappings, te::concept_map<C, T>.map_);
  });
  return boost::hana::unpack(merged, [](auto ...m) {
    return te::concept_map_t<Concept, T, decltype(m)...>{m...};
  });
}

} // end namespace te

#endif // TE_CONCEPT_MAP_HPP
