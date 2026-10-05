#include "skeleton_mesh_contour.h"
#include "core/math/geometry_2d.h"
#include "scene/resources/bit_map.h"
#include "scene/resources/image_texture.h"

namespace SkeletonMeshContour {
// Closed-ring RDP with shared split endpoints. BitMap's trace can retain pairs
// of adjacent pixel corners even after its initial simplification.
static Vector<Vector2> simplify_outline(const Vector<Vector2> &ring, real_t tolerance) {
    if(ring.size()<4) { return ring; }
    int opposite=1;
    for(int i=2;i<ring.size();i++) { if(ring[i].distance_squared_to(ring[0])>ring[opposite].distance_squared_to(ring[0])) { opposite=i; } }
    Vector<uint8_t> keep; keep.resize(ring.size()); keep.fill(0); keep.write[0]=1; keep.write[opposite]=1;
    Vector<Vector2i> pending; pending.push_back(Vector2i(0,opposite)); pending.push_back(Vector2i(opposite,ring.size()));
    while(!pending.is_empty()) {
        Vector2i range=pending[pending.size()-1]; pending.remove_at(pending.size()-1);
        Vector2 segment[2]={ring[range.x],ring[range.y%ring.size()]};
        real_t farthest=tolerance*tolerance; int split=-1;
        for(int i=range.x+1;i<range.y;i++) {
            real_t distance=ring[i].distance_squared_to(Geometry2D::get_closest_point_to_segment(ring[i],segment));
            if(distance>farthest) { farthest=distance; split=i; }
        }
        if(split>=0) { keep.write[split]=1; pending.push_back(Vector2i(range.x,split)); pending.push_back(Vector2i(split,range.y)); }
    }
    Vector<Vector2> result; for(int i=0;i<ring.size();i++) { if(keep[i]) { result.push_back(ring[i]); } }
    return result.size()>=3?result:ring;
}
Dictionary generate(const Dictionary &mesh,float threshold,float precision,int margin,String &error) {
    error=String();
    if(!Math::is_finite(threshold) || threshold<0 || threshold>=1 || !Math::is_finite(precision) || precision<.1 || precision>32 || margin<0 || margin>16) {
        error=TTR("Invalid contour settings."); return Dictionary();
    }
    Ref<Texture2D> texture=mesh.get("texture",Variant());
    Ref<Image> image=texture.is_valid()?texture->get_image():Ref<Image>();
    if(image.is_null() || image->is_empty()) { error=TTR("Select an attachment with a readable image."); return Dictionary(); }
    if(int64_t(image->get_width())*image->get_height()>16777216) { error=TTR("Automatic contour supports images up to 16 million pixels."); return Dictionary(); }
    image=image->duplicate();
    if(image->is_compressed() && image->decompress()!=OK) { error=TTR("Could not decompress the attachment image."); return Dictionary(); }
    PackedVector2Array points=mesh.get("polygon",PackedVector2Array()),uv=mesh.get("uv",PackedVector2Array());
    PackedInt32Array faces=mesh.get("triangles",PackedInt32Array()),bones=mesh.get("bones",PackedInt32Array());
    PackedFloat32Array weights=mesh.get("weights",PackedFloat32Array());
    if(points.size()<3 || uv.size()!=points.size() || (!weights.is_empty() && (weights.size()!=points.size()*4 || bones.size()!=weights.size()))) {
        error=TTR("The attachment needs a valid mesh and texture coordinates."); return Dictionary();
    }
    if(faces.is_empty()) { faces=Geometry2D::triangulate_polygon(points); }
    if(faces.size()%3 || faces.is_empty()) { error=TTR("The attachment mesh cannot be triangulated."); return Dictionary(); }
    for(int index:faces) { if(index<0 || index>=points.size()) { error=TTR("The attachment has invalid triangle indices."); return Dictionary(); } }
    Ref<BitMap> mask; mask.instantiate(); mask->create_from_image_alpha(image,threshold);
    if(mask->get_true_bit_count()==0) { error=TTR("No visible pixels at this alpha threshold."); return Dictionary(); }
    Rect2i bounds(Vector2i(),image->get_size());
    if(margin) { mask->grow_mask(margin,bounds); }
    Vector<Vector<Vector2>> contours=mask->clip_opaque_to_polygons(bounds,precision);
    for(int i=0;i<contours.size();i++) { contours.write[i]=simplify_outline(contours[i],precision); }
    if(int64_t(contours.size())*(faces.size()/3)>250000) { error=TTR("The contour is too complex. Increase the simplification distance."); return Dictionary(); }
    const Vector2 size(image->get_width(),image->get_height());
    Vector<Vector<Vector2>> source_triangles;
    for(int t=0;t<faces.size();t+=3) {
        Vector<Vector2> triangle; for(int j=0;j<3;j++) { triangle.push_back(uv[faces[t+j]]*size); }
        if(Math::abs((triangle[1]-triangle[0]).cross(triangle[2]-triangle[0]))<.000001) { continue; }
        if(Geometry2D::is_polygon_clockwise(triangle)) { triangle.reverse(); }
        source_triangles.push_back(triangle);
    }
    // Remove internal mesh edges before clipping, avoiding numerical slivers at old cuts.
    Vector<Vector<Vector2>> footprint,footprint_holes,clipped;
    Geometry2D::merge_many_polygons(source_triangles,footprint,footprint_holes);
    for(const Vector<Vector2> &area:footprint) {
        Rect2 tri_bounds(area[0],Vector2()); for(const Vector2 &point:area) { tri_bounds.expand_to(point); }
        for(const Vector<Vector2> &contour:contours) {
            Rect2 contour_bounds(contour[0],Vector2()); for(const Vector2 &point:contour) { contour_bounds.expand_to(point); }
            if(!tri_bounds.intersects(contour_bounds,true)) { continue; }
            for(Vector<Vector2> piece:Geometry2D::intersect_polygons(area,contour)) {
                if(Geometry2D::is_polygon_clockwise(piece)) { piece.reverse(); }
                clipped.push_back(piece);
            }
        }
    }
    // Union before triangulating: old triangle cuts must not become dense outline vertices.
    Vector<Vector<Vector2>> outlines,holes;
    Geometry2D::merge_many_polygons(clipped,outlines,holes);
    // Existing mesh cutouts remain cutouts, even when the image is opaque there.
    for(const Vector<Vector2> &hole:footprint_holes) {
        Vector<Vector<Vector2>> remaining;
        for(const Vector<Vector2> &outline:outlines) {
            for(const Vector<Vector2> &piece:Geometry2D::clip_polygons(outline,hole)) {
                if(Geometry2D::is_polygon_clockwise(piece)) { holes.push_back(piece); }
                else { remaining.push_back(piece); }
            }
        }
        outlines=remaining;
    }
    if(outlines.is_empty()) { error=TTR("No visible contour intersects this attachment's texture region."); return Dictionary(); }
    Vector<Vector<Vector2>> regions=Geometry2D::decompose_many_polygons_in_convex(outlines,holes);
    PackedVector2Array output,output_uv;
    PackedInt32Array output_faces,output_bones;
    PackedFloat32Array output_weights;
    HashMap<Vector2i,int> vertices_by_pixel;
    auto add_vertex=[&](const Vector2 &pixel) -> int {
        Vector2i key(Math::round(pixel.x*1000),Math::round(pixel.y*1000));
        if(vertices_by_pixel.has(key)) { return vertices_by_pixel[key]; }
        int chosen=-1; real_t best=1e30; Vector3 bary;
        for(int t=0;t<faces.size();t+=3) {
            Vector2 a=uv[faces[t]]*size,b=uv[faces[t+1]]*size,c=uv[faces[t+2]]*size;
            real_t determinant=(b-a).cross(c-a); if(Math::abs(determinant)<.000001) { continue; }
            real_t y=(pixel-a).cross(c-a)/determinant,z=(b-a).cross(pixel-a)/determinant;
            Vector3 candidate(1-y-z,y,z);
            real_t penalty=MAX(real_t(0),-candidate.x)+MAX(real_t(0),-candidate.y)+MAX(real_t(0),-candidate.z);
            if(penalty<best) { best=penalty; chosen=t; bary=candidate; if(penalty==0) { break; } }
        }
        if(chosen<0 || output.size()>=16384) { return -1; }
        int indices[3]={faces[chosen],faces[chosen+1],faces[chosen+2]};
        Vector2 local=points[indices[0]]*bary.x+points[indices[1]]*bary.y+points[indices[2]]*bary.z;
                    int influence_bones[4]={0,0,0,0}; real_t influence_weights[4]={0,0,0,0};
                    if(!weights.is_empty()) {
                        HashMap<int,real_t> influences;
                        Vector3 blend(MAX(real_t(0),bary.x),MAX(real_t(0),bary.y),MAX(real_t(0),bary.z)); blend/=blend.x+blend.y+blend.z;
                        for(int k=0;k<3;k++) { for(int j=0;j<4;j++) { int offset=indices[k]*4+j; influences[bones[offset]]+=weights[offset]*blend[k]; } }
                        for(const KeyValue<int,real_t> &entry:influences) { for(int j=0;j<4;j++) { if(entry.value>influence_weights[j]) { for(int k=3;k>j;k--) { influence_weights[k]=influence_weights[k-1]; influence_bones[k]=influence_bones[k-1]; } influence_weights[j]=entry.value; influence_bones[j]=entry.key; break; } } }
                        real_t sum=0; for(real_t weight:influence_weights) { sum+=weight; }
                        if(sum<=0) { error=TTR("The attachment has invalid skin weights."); return -1; }
                        for(int j=0;j<4;j++) { influence_weights[j]/=sum; }
                        // Canonical order makes adjacent triangle intersections weld reliably.
                        for(int j=0;j<4;j++) { for(int k=j+1;k<4;k++) { if(influence_bones[k]<influence_bones[j]) { SWAP(influence_bones[k],influence_bones[j]); SWAP(influence_weights[k],influence_weights[j]); } } }
                    }

        int vertex=output.size(); output.push_back(local); output_uv.push_back(pixel/size); vertices_by_pixel[key]=vertex;
        if(!weights.is_empty()) { for(int j=0;j<4;j++) { output_bones.push_back(influence_bones[j]); output_weights.push_back(influence_weights[j]); } }
        return vertex;
    };
    for(const Vector<Vector2> &region:regions) {
        Vector<int> triangles=Geometry2D::triangulate_polygon(region),remap;
        for(const Vector2 &pixel:region) { int vertex=add_vertex(pixel); if(vertex<0) { error=TTR("The contour is too complex. Increase the simplification distance."); return Dictionary(); } remap.push_back(vertex); }
        for(int index:triangles) { output_faces.push_back(remap[index]); }
    }
    // Keep well-spaced interior control points, but do not carry the old edge intersections over.
    const real_t spacing=MAX(real_t(4),real_t(precision)*2);
    for(int i=0;i<uv.size();i++) {
        Vector2 pixel=uv[i]*size; bool near=false;
        for(const Vector2 &point:output_uv) { if((point*size).distance_to(pixel)<spacing) { near=true; break; } }
        if(near) { continue; }
        for(const Vector<Vector2> &ring:outlines) { for(int j=0;j<ring.size();j++) { Vector2 segment[2]={ring[j],ring[(j+1)%ring.size()]}; if(Geometry2D::get_closest_point_to_segment(pixel,segment).distance_to(pixel)<spacing) { near=true; break; } } if(near) { break; } }
        if(near) { continue; }
        PackedInt32Array updated; int inserted=-1;
        for(int t=0;t<output_faces.size();t+=3) {
            int a=output_faces[t],b=output_faces[t+1],c=output_faces[t+2];
            Vector2 pa=output_uv[a]*size,pb=output_uv[b]*size,pc=output_uv[c]*size;
            real_t determinant=(pb-pa).cross(pc-pa);
            if(Math::abs(determinant)<.000001) { continue; }
            real_t y=(pixel-pa).cross(pc-pa)/determinant,z=(pb-pa).cross(pixel-pa)/determinant;
            if(y>=-.00001 && z>=-.00001 && y+z<=1.00001) {
                if(inserted<0) { inserted=add_vertex(pixel); if(inserted<0) { error=TTR("The contour is too complex. Increase the simplification distance."); return Dictionary(); } }
                int corners[3]={a,b,c};
                for(int j=0;j<3;j++) { int from=corners[j],to=corners[(j+1)%3]; if(Math::abs(((output_uv[to]-output_uv[from])*size).cross(pixel-output_uv[from]*size))>.00001) { updated.push_back(from); updated.push_back(to); updated.push_back(inserted); } }
            } else { updated.push_back(a); updated.push_back(b); updated.push_back(c); }
        }
        if(inserted>=0) { output_faces=updated; }
    }
    if(output_faces.is_empty()) { error=TTR("No visible contour intersects this attachment's texture region."); return Dictionary(); }
    if(output_faces.size()>output.size()*12) { error=TTR("The contour is too complex. Increase the simplification distance."); return Dictionary(); }
    Dictionary result=mesh.duplicate(true);
    result["polygon"]=output; result["uv"]=output_uv; result["triangles"]=output_faces;
    result["bones"]=output_bones; result["weights"]=output_weights;
    return result;
}

bool self_test() {
    Ref<Image> image=Image::create_empty(64,64,false,Image::FORMAT_RGBA8);
    image->fill(Color(0,0,0,0));
    image->fill_rect(Rect2i(8,8,12,40),Color(1,1,1));
    image->fill_rect(Rect2i(8,36,34,12),Color(1,1,1));
    image->fill_rect(Rect2i(50,8,8,10),Color(1,1,1));
    Dictionary mesh; PackedVector2Array uv({Vector2(0,0),Vector2(1,0),Vector2(1,1),Vector2(0,1),Vector2(.5,.5)}),points;
    PackedInt32Array bones; PackedFloat32Array weights;
    for(const Vector2 &point:uv) {
        points.push_back(Vector2(60-point.x*128,point.y*192-20));
        for(int j=0;j<4;j++) { bones.push_back(j==1?1:0); weights.push_back(j==0?1-point.x:j==1?point.x:0); }
    }
    mesh["texture"]=ImageTexture::create_from_image(image); mesh["polygon"]=points; mesh["uv"]=uv;
    mesh["triangles"]=PackedInt32Array({0,1,4,1,2,4,2,3,4,3,0,4}); mesh["bones"]=bones; mesh["weights"]=weights;
    String error; Dictionary result=generate(mesh,.1,.1,0,error);
    if(result.is_empty()) { ERR_PRINT(error); return false; }
    PackedVector2Array new_points=result["polygon"],new_uv=result["uv"];
    PackedInt32Array new_bones=result["bones"],triangles=result["triangles"]; PackedFloat32Array new_weights=result["weights"];
    bool ok=PackedVector2Array(mesh["polygon"])==points && result["texture"]==mesh["texture"];
    for(int i=0;i<new_points.size();i++) {
        ok &= new_points[i].distance_to(Vector2(60-new_uv[i].x*128,new_uv[i].y*192-20))<.001;
        real_t sum=0,right=0;
        for(int j=0;j<4;j++) { sum+=new_weights[i*4+j]; if(new_bones[i*4+j]==1) { right+=new_weights[i*4+j]; } ok &= new_weights[i*4+j]>=0; }
        ok &= Math::abs(sum-1)<.0001 && Math::abs(right-new_uv[i].x)<.0001;
    }
    auto covered=[&](const Vector2 &point) {
        for(int t=0;t<triangles.size();t+=3) {
            PackedVector2Array triangle; for(int j=0;j<3;j++) { triangle.push_back(new_uv[triangles[t+j]]*64); }
            if(Geometry2D::is_point_in_polygon(point,triangle)) { return true; }
        }
        return false;
    };
    ok &= covered(Vector2(10,10)) && covered(Vector2(35,44)) && covered(Vector2(54,13));
    ok &= !covered(Vector2(32,20)) && !covered(Vector2(47,30));
    // A cropped atlas region must not pull in another attachment's opaque island.
    Dictionary cropped=mesh.duplicate(true); PackedVector2Array region=uv;
    for(int i=0;i<region.size();i++) { region.set(i,region[i]*Vector2(.75,1)); }
    cropped["uv"]=region; Dictionary limited=generate(cropped,.1,1,0,error);
    ok &= !limited.is_empty(); if(!limited.is_empty()) { for(const Vector2 &point:PackedVector2Array(limited["uv"])) { ok &= point.x<=.75; } }
    Dictionary empty=mesh.duplicate(true); Ref<Image> transparent=Image::create_empty(8,8,false,Image::FORMAT_RGBA8); transparent->fill(Color(0,0,0,0)); empty["texture"]=ImageTexture::create_from_image(transparent);
    ok &= generate(empty,.1,2,1,error).is_empty() && !error.is_empty();
    ok &= generate(mesh,.1,-1,0,error).is_empty();
    // Old diagonal intersections must disappear; keep the useful central control point.
    Ref<Image> rectangle=Image::create_empty(64,64,false,Image::FORMAT_RGBA8);
    rectangle->fill(Color(0,0,0,0)); rectangle->fill_rect(Rect2i(8,8,48,48),Color(1,1,1));
    Dictionary simple=mesh.duplicate(true); simple["texture"]=ImageTexture::create_from_image(rectangle);
    Dictionary sparse=generate(simple,.1,4,2,error);
    ok &= !sparse.is_empty();
    if(!sparse.is_empty()) {
        PackedVector2Array sparse_uv=sparse["uv"];
        ok &= sparse_uv.size()<=9 && sparse_uv.has(Vector2(.5,.5));
        Dictionary repeated=generate(sparse,.1,4,2,error);
        ok &= !repeated.is_empty() && PackedVector2Array(repeated.get("uv",PackedVector2Array())).size()<=sparse_uv.size();
    }
    print_line(ok?"SKELETON_AUTO_CONTOUR_PASS concave islands atlas_uv weights source_unchanged empty_guard sparse_repeat":"SKELETON_AUTO_CONTOUR_FAIL");
    return ok;
}
}
