#pragma once
#include "scene/resources/animation.h"
namespace ECSAnimationGraph {
bool validate(const Dictionary &p_graph, const Ref<Animation> &p_layout);
Variant sample(const Dictionary &p_graph, int p_track, double p_time);
Array events(const Dictionary &p_graph,double p_from,double p_to,bool p_include_start);
void advance(Dictionary &p_graph,double p_delta);
bool test();
}
