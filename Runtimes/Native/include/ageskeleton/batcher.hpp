// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#pragma once
#include "meshclip.hpp"
namespace ageskeleton {
struct Batch {
    int texture=0;
    std::vector<int> attachments,indices;
    std::vector<float> positions,uv,colors;
};
// Stable painter order. Never merge A/B/A into A/A/B, even when pages match.
class Batcher {
    std::vector<int> previous,current;
public:
    std::vector<Batch> batches;
    size_t revision=0;
    bool update(const Player &p) {
        current.clear();
        for(size_t i=0;i<p.visible.size();++i) if(p.visible[i]) current.push_back(int(i));
        std::sort(current.begin(),current.end(),[&](int a,int b){return p.orders[a]!=p.orders[b]?p.orders[a]<p.orders[b]:a<b;});
        bool changed=current!=previous;
        if(changed) {
            batches.clear();previous=current;++revision;
            for(int i:current) {
                const auto &a=p.data().attachments[i];
                if(batches.empty() || batches.back().texture!=a.texture) {batches.emplace_back();batches.back().texture=a.texture;}
                auto &b=batches.back();int offset=int(b.positions.size()/2);b.attachments.push_back(i);
                b.positions.resize((offset+a.count)*2);b.colors.resize((offset+a.count)*4);
                b.uv.insert(b.uv.end(),a.uv.begin(),a.uv.end());for(int k:a.triangles)b.indices.push_back(offset+k);
            }
        }
        for(auto &b:batches) {size_t v=0;for(int i:b.attachments){const auto &a=p.data().attachments[i];
            std::copy_n(p.positions.begin()+a.offset*2,a.count*2,b.positions.begin()+v*2);
            for(int k=0;k<a.count;++k)std::copy_n(p.colors.begin()+i*4,4,b.colors.begin()+(v+k)*4);
            v+=a.count;
        }}
        return changed;
    }
};
}
