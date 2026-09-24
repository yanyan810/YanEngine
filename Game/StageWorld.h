#pragma once
#include "Raycast.h"
#include <string>
#include <limits>
#include <vector>

inline Vector3 StagePoint(const Vector3& p,const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
inline float StageLength(const Vector3& p) { return std::hypot(p.x,p.y,p.z); }
inline float StageDot(const Vector3& a,const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
struct StageCollider {
    std::string id;
    Vector3 position{}, rotation{}, scale{1,1,1};
    AABB local{};
    Matrix4x4 world=Matrix4x4::MakeIdentity4x4(), inverse=Matrix4x4::MakeIdentity4x4();
};
struct StageHit { float distance=0; Vector3 normal{}; };
class StageWorld {
public:
    std::vector<StageCollider> colliders;
    // Local-space slabs preserve rotated and nonuniformly scaled boxes.
    static bool CastBox(const StageCollider& c,const Vector3& origin,const Vector3& unit,float range,
        const Vector3& padding,StageHit& hit) {
        const auto o=StagePoint(origin,c.inverse);
        const auto d=StagePoint(origin+unit,c.inverse)-o;
        const float a[]={o.x,o.y,o.z}, b[]={d.x,d.y,d.z};
        const float low[]={c.local.min.x-padding.x,c.local.min.y-padding.y,c.local.min.z-padding.z};
        const float high[]={c.local.max.x+padding.x,c.local.max.y+padding.y,c.local.max.z+padding.z};
        float enter=0,leave=range;
        int face=-1; float sign=0;
        for (int i=0;i<3;++i) {
            if (std::abs(b[i])<1e-8f) { if (a[i]<low[i] || a[i]>high[i]) return false; continue; }
            float entryDistance=(low[i]-a[i])/b[i], exitDistance=(high[i]-a[i])/b[i];
            float side=-1;
            if (entryDistance>exitDistance) { std::swap(entryDistance,exitDistance); side=1; }
            if (entryDistance>=enter) { enter=entryDistance; face=i; sign=side; }
            leave=std::min(leave,exitDistance);
            if (enter>leave) return false;
        }
        if (face<0) {
            // Origin inside: use the nearest exit plane, for deterministic depenetration.
            float nearest=std::numeric_limits<float>::max();
            for (int i=0;i<3;++i) for (int side : {-1,1}) {
                const float gap=side<0 ? a[i]-low[i] : high[i]-a[i];
                if (gap<nearest) { nearest=gap; face=i; sign=static_cast<float>(side); }
            }
        }
        Vector3 n{c.inverse.m[0][face]*sign,c.inverse.m[1][face]*sign,c.inverse.m[2][face]*sign};
        const float length=StageLength(n);
        hit={enter,length>0 ? n*(1/length) : Vector3{}}; return true;
    }
    bool Raycast(const Vector3& origin,const Vector3& direction,float range,StageHit& hit,float radius=0) const {
        const float length=StageLength(direction);
        if (!std::isfinite(length) || length<=0 || !std::isfinite(range) || range<0) return false;
        const auto unit=direction*(1/length);
        bool found=false;
        for (const auto& c : colliders) {
            Vector3 padding{};
            padding.x=radius*std::hypot(c.inverse.m[0][0],c.inverse.m[1][0],c.inverse.m[2][0]);
            padding.y=radius*std::hypot(c.inverse.m[0][1],c.inverse.m[1][1],c.inverse.m[2][1]);
            padding.z=radius*std::hypot(c.inverse.m[0][2],c.inverse.m[1][2],c.inverse.m[2][2]);
            StageHit candidate;
            if (CastBox(c,origin,unit,range,padding,candidate)) { found=true; hit=candidate; range=hit.distance; }
        }
        return found;
    }
    // Upright cylinder projected into local OBB slabs (conservative at corners).
    // Swept motion avoids tunnelling even with long frames. Y remains controlled by FPS movement.
    Vector3 Move(const Vector3& start,const Vector3& desired,float radius=.4f,float height=1.8f) const {
        Vector3 position=start;
        Vector3 remaining=desired-start; remaining.y=0;
        for (int iteration=0;iteration<6;++iteration) {
            const float length=StageLength(remaining);
            if (length<1e-6f) break;
            const auto direction=remaining*(1/length);
            StageHit nearest{length,{}}; bool blocked=false;
            for (const auto& c : colliders) {
                const auto bounds=TransformAABB(c.local,c.world);
                if (bounds.max.y<=position.y+.01f || bounds.min.y>=position.y+height-.01f) continue;
                float extent[3];
                for (int i=0;i<3;++i) extent[i]=radius*std::hypot(c.inverse.m[0][i],c.inverse.m[2][i])+
                    height*.5f*std::abs(c.inverse.m[1][i]);
                StageHit candidate;
                if (CastBox(c,position+Vector3{0,height*.5f,0},direction,nearest.distance,{extent[0],extent[1],extent[2]},candidate)) {
                    candidate.normal.y=0;
                    const float n=StageLength(candidate.normal);
                    if (n<1e-6f || StageDot(direction,candidate.normal)>=-1e-6f) continue;
                    candidate.normal*=1/n; nearest=candidate; blocked=true;
                }
            }
            if (!blocked) { position+=remaining; break; }
            const float travel=std::max(0.0f,nearest.distance-.002f);
            position+=direction*travel;
            remaining-=direction*travel;
            const float inward=StageDot(remaining,nearest.normal);
            if (inward<0) remaining-=nearest.normal*inward;
        }
        position.y=desired.y;
        return position;
    }
};
