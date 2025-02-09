//=============================================================================
// Copyright (C) 2011-2018 The pmp-library developers
//
// This file is part of the Polygon Mesh Processing Library.
// Distributed under a MIT-style license, see LICENSE.txt for details.
//
// SPDX-License-Identifier: MIT-with-employer-disclaimer
//=============================================================================

// clang-format off

static const char* phong_vshader =
#ifndef __EMSCRIPTEN__
    "#version 330\n"
#else
    "#version 300 es\n"
#endif
    "\n"
    "layout (location=0) in vec4 v_position;\n"
    "layout (location=1) in vec3 v_normal;\n"
    "layout (location=2) in vec2 v_tex;\n"
    "out vec3 v2f_normal;\n"
    "out vec2 v2f_tex;\n"
    "out vec3 v2f_view;\n"
    "uniform mat4 modelview_projection_matrix;\n"
    "uniform mat4 modelview_matrix;\n"
    "uniform mat3 normal_matrix;\n"
    "uniform float point_size;\n"
    "uniform bool show_texture_layout;\n"
    "\n"
    "void main()\n"
    "{\n"
    "   v2f_normal  = normal_matrix * v_normal;\n"
    "   v2f_tex     = v_tex;\n"
    "   vec4 pos    = show_texture_layout ? vec4(v_tex, 0.0, 1.0) : v_position;\n"
    "   v2f_view    = -(modelview_matrix * pos).xyz;\n"
    "   gl_PointSize = point_size;\n"
    "   gl_Position = modelview_projection_matrix * pos;\n"
    "} \n";


static const char* phong_fshader =
#ifndef __EMSCRIPTEN__
    "#version 330\n"
#else
    "#version 300 es\n"
    "precision mediump float;\n"
#endif
    "\n"
    "in vec3  v2f_normal;\n"
    "in vec2  v2f_tex;\n"
    "in vec3  v2f_view;\n"
    "\n"
    "uniform bool   use_lighting;\n"
    "uniform bool   use_texture;\n"
    "uniform bool   use_srgb;\n"
    "uniform vec3   front_color;\n"
    "uniform vec3   back_color;\n"
    "uniform float  ambient;\n"
    "uniform float  diffuse;\n"
    "uniform float  specular;\n"
    "uniform float  shininess;\n"
    "uniform float  alpha;\n"
    "uniform vec3   light1;\n"
    "uniform vec3   light2;\n"
    "\n"
    "uniform sampler2D mytexture;\n"
    "\n"
    "out vec4 f_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec3 color = gl_FrontFacing ? front_color : back_color;\n"
    "    vec3 rgb;\n"
    "\n"
    "    if (use_lighting)\n"
    "    {\n"
    "       vec3 L1 = normalize(light1);\n"
    "       vec3 L2 = normalize(light2);\n"
    "       vec3 N  = normalize(v2f_normal);\n"
    "       vec3 V  = normalize(v2f_view);\n"
    "       \n"
    //"       if (!gl_FrontFacing) N = -N;\n" // gl_FrontFacing does not work on Mario's new MacBook
    "       if (dot(N,V)<0.0) N = -N;\n"
    "       \n"
    "       vec3  R;\n"
    "       float NL, RV;\n"
    "       \n"
    "       rgb = ambient * 0.1 * color;\n"
    "       \n"
    "       NL = dot(N, L1);\n"
    "       if (NL > 0.0)\n"
    "       {\n"
    "           rgb += diffuse * NL * color;\n"
    "           R  = normalize(-reflect(L1, N));\n"
    "           RV = dot(R, V);\n"
    "           if (RV > 0.0) \n"
    "           {\n"
    "               rgb += vec3( specular * pow(RV, shininess) );\n"
    "           }\n"
    "       }\n"
    "       \n"
    "       NL = dot(N, L2);\n"
    "       if (NL > 0.0)\n"
    "       {\n"
    "            rgb += diffuse * NL * color;\n"
    "            R  = normalize(-reflect(L2, N));\n"
    "            RV = dot(R, V);\n"
    "            if (RV > 0.0) \n"
    "            {\n"
    "                rgb += vec3( specular * pow(RV, shininess) );\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "   \n"
    "    // do not use lighting\n"
    "    else\n"
    "    {\n"
    "        rgb = color;\n"
    "    }\n"
    "    \n"
    "   if (use_texture) rgb *= texture(mytexture, v2f_tex).xyz;\n"
    "   if (use_srgb)    rgb  = pow(clamp(rgb, 0.0, 1.0), vec3(0.45));\n"
    "   \n"
    "    f_color = vec4(rgb, alpha);\n"
    "}";


//=============================================================================
// clang-format on
//=============================================================================
