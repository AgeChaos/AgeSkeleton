#pragma once
#include "scene/resources/image_texture.h"
#include "editor/themes/editor_scale.h"

// Original vector glyphs, scoped to the skeleton workspace (not editor-wide icons).
inline Ref<Texture2D> skeleton_workspace_icon(const String &kind, float logical_size = 16) {
	String shape;
	if(kind == "keyframe") {
        shape="<path d='M8 2L14 8L8 14L2 8Z' fill='none' stroke='#ffba77' stroke-width='1.7'/><circle cx='8' cy='8' r='1.6' fill='#ffba77'/>";
    } else if(kind == "select") {
        shape="<path d='M3 1L13 9L8 10L6 15Z' fill='none' stroke='#dedede' stroke-width='1.5' stroke-linejoin='round'/>";
    } else if(kind == "brush") {
        shape="<path d='M7 9L12 2Q15 0 15 3L10 11Z' fill='#85ced4'/><path d='M7 9Q2 8 3 12Q3 14 1 14Q8 17 10 11Z' fill='none' stroke='#85ced4' stroke-width='1.3'/>";
    } else if(kind == "pan") {
        shape="<path d='M5 8V4Q6 2 7 4V7V2Q8 0 9 2V7V3Q10 1 11 3V8V5Q13 3 13 6V11Q13 15 9 15H7L2 10Q1 8 3 8L5 10' fill='none' stroke='#dedede' stroke-width='1.6' stroke-linecap='round'/>";
    } else if(kind == "eye") {
        shape="<path d='M1 8Q8 -1 15 8Q8 17 1 8Z' fill='none' stroke='#c7dbe4' stroke-width='1.3'/><circle cx='8' cy='8' r='2.5' fill='#c7dbe4'/>";
    } else if(kind == "position") {
        shape="<path d='M8 1V15M1 8H15M5 4L8 1L11 4M5 12L8 15L11 12M4 5L1 8L4 11M12 5L15 8L12 11' fill='none' stroke='#85ced4' stroke-width='1.5'/>";
    } else if(kind == "rotation") {
        shape="<path d='M12 5A5 5 0 1 0 13 10M9 5H13V1' fill='none' stroke='#a4c9e8' stroke-width='1.7' stroke-linejoin='round'/>";
    } else if(kind == "scale") {
        shape="<path d='M2 7V14H9M7 2H14V9M6 10L14 2' fill='none' stroke='#ecb4bd' stroke-width='1.6'/><path d='M2 7H9V14' fill='none' stroke='#ecb4bd'/>";
    } else if(kind == "shear") {
        shape="<path d='M6 3H14L10 13H2ZM2 15H13M3 1H14' fill='none' stroke='#ffba77' stroke-width='1.4'/>";
    } else if(kind == "color" || kind == "dark") {
        shape="<circle cx='8' cy='8' r='6' fill='#de78c8'/><path d='M8 2A6 6 0 0 1 14 8H8Z' fill='#a4c9e8'/><path d='M8 8H14A6 6 0 0 1 8 14Z' fill='#85ced4'/><circle cx='8' cy='8' r='2' fill='#eee'/>";
    } else if(kind == "z_index") {
        shape="<path d='M2 5L8 2L14 5L8 8ZM2 8L8 11L14 8M2 11L8 14L14 11' fill='none' stroke='#eab77e' stroke-width='1.5'/>";
    } else if (kind == "mode_setup") {
		shape = "<circle cx='8' cy='2' r='1.5' fill='white'/><path d='M8 5V9M2 5L8 5L14 5M8 9L5 15M8 9L11 15' stroke='white' stroke-width='1.6' stroke-linecap='round' fill='none'/>";
	} else if (kind == "mode_animation") {
		shape = "<circle cx='10' cy='2' r='1.5' fill='white'/><path d='M9 5L7 9L11 11L10 15M7 9L4 12L1 12M5 4L8 5L11 7L14 6' stroke='white' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M2 5L4 5M1 8L3 8' stroke='white' stroke-opacity='.35' fill='none'/>";
	} else if (kind == "skin") {
        shape="<path d='M6 4C6 0 11 0 11 3C11 5 8 5 8 7L2 12Q1 14 3 14H13Q15 14 14 12L8 7' fill='none' stroke='#efa960' stroke-width='1.5' stroke-linecap='round'/>";
    } else if (kind == "placeholder") {
        shape="<path d='M3 3H13V13H3Z' fill='none' stroke='#e5a860' stroke-width='1.4' stroke-dasharray='3 2'/><path d='M6 8H10M8 6V10' stroke='#efcc99'/>";
    } else if (kind == "mesh") {
        shape = "<path d='M2 3L13 2L14 13L3 14ZM2 3L14 13M13 2L3 14M2 3L3 14' fill='none' stroke='#72ced5' stroke-width='1.2'/><circle cx='8' cy='8' r='1.5' fill='#d5f5f5'/>";
    } else if (kind == "bone") {
		shape = "<path d='M3 13L7 6L13 3L10 9Z' fill='#dadede' stroke='#a0aaaa'/><circle cx='3' cy='13' r='2' fill='#343838' stroke='#dadede'/>";
	} else if (kind == "slot") {
		shape = "<path d='M8 2L14 6L11 13H5L2 6Z' fill='none' stroke='#bac3c3' stroke-width='1.5'/><circle cx='8' cy='7' r='2' fill='#777f7f'/>";
	} else if (kind == "attachment") {
		shape = "<path d='M3 3H13V13H3Z' fill='#6abec1' stroke='#e0eeee'/><path d='M5 11L8 7L11 11' fill='none' stroke='#e0eeee'/>";
	} else if (kind == "animation") {
		shape = "<path d='M4 12L8 8L12 4' fill='none' stroke='#9ecacc'/><circle cx='4' cy='12' r='2' fill='#dbe5e5'/><circle cx='8' cy='8' r='2' fill='#91bfc2'/><circle cx='12' cy='4' r='2' fill='#dbe5e5'/>";
	} else if (kind == "sheet") {
		shape = "<path d='M2 2H5V5H2ZM7 2H10V5H7ZM12 2H15V5H12ZM2 7H5V10H2Z' fill='#61b7bf'/><path d='M7 7H10V10H7ZM12 7H15V10H12ZM2 12H5V15H2Z' fill='#a58ad0'/><path d='M7 12H10V15H7ZM12 12H15V15H12Z' fill='#a7cd66'/>";
	} else {
		shape = "<circle cx='8' cy='3' r='2' fill='#dbe2e2'/><path d='M8 6V10M3 7L8 6L13 7M8 10L4 14M8 10L12 14' stroke='#dbe2e2' stroke-width='1.5' fill='none'/>";
	}
	Ref<Image> image;
	image.instantiate();
	image->load_svg_from_string("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>" + shape + "</svg>", EDSCALE * logical_size / 16);
	return ImageTexture::create_from_image(image);
}
