#ifndef GENESIS_GPU_RASTER_SHADER_H
#define GENESIS_GPU_RASTER_SHADER_H
static const char genesis_gpu_vertex[]=
"#version 330\n"
"void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}\n";
static const char genesis_gpu_fragment[]=
"#version 330\n"
"uniform usamplerBuffer data;uniform int rows;out vec4 result;\n"
"uint U(uint i){return texelFetch(data,int(i)).r;}\n"
"uint B(uint r,uint lane,uint x){return (U(U(r+11u)+lane*U(r+10u)+(x>>2u))>>((x&3u)*8u))&255u;}\n"
"uint plane(uint r,uint lane,uint x){\n"
" uint n=B(r,lane,x);uint p=U(r+12u+lane)+n*2u;uint bits=U(p),m=U(p+1u);\n"
" uint fx=((m>>16u)&7u)+x-(m&65535u);if((m&0x01000000u)!=0u)fx^=7u;\n"
" uint nib=(bits>>((7u-fx)*4u))&15u;return ((m>>24u)&176u)|nib|(nib!=0u?64u:0u);}\n"
"void main(){uint x=uint(gl_FragCoord.x),y=uint(gl_FragCoord.y);if(y>=uint(rows))discard;\n"
" uint r=1536u+y*16u;if(x>=U(r))discard;uint pixel;uint start=U(r+1u),count=U(r+2u);\n"
" if(x<start||x>=start+count)pixel=U(r+4u);else if(U(r+5u)==0u)pixel=U(r+3u);else{\n"
"  uint a=U(r+15u)!=0u?plane(r,0u,x):B(r,0u,x);\n"
"  uint b=U(r+15u)!=0u?plane(r,1u,x):B(r,1u,x);\n"
"  uint s=B(r,2u,x);bool so=B(r,3u,x)!=0u,sh=B(r,4u,x)!=0u&&so;\n"
"  bool ah=(a&192u)==192u,bh=(b&192u)==192u;\n"
"  pixel=sh?s:ah?a&63u:bh?b&63u:so?s:(a&64u)!=0u?a&63u:(b&64u)!=0u?b&63u:U(r+3u);\n"
"  if(U(r+6u)!=0u&&pixel<64u){bool shadow=!(ah||bh||sh),hilite=false;\n"
"   if(B(r,5u,x)!=0u)shadow=true;\n"
"   if(B(r,6u,x)!=0u){if(shadow)shadow=false;else hilite=true;}\n"
"   if(hilite)pixel+=128u;else if(shadow)pixel+=64u;}\n"
"  if(U(r+7u)!=0u&&pixel<193u&&((x-U(r+8u))&U(r+9u))==0u)pixel=(pixel&63u)+64u;\n"
" }uint color=0xff000000u;if(pixel<192u){\n"
"  uint c=(U(U(r+14u)+((pixel&63u)>>1u))>>((pixel&1u)*16u))&65535u;\n"
"  uint index=((c>>1u)&7u)|((c>>2u)&56u)|((c>>3u)&448u);color=U((pixel>>6u)*512u+index);}\n"
" result=vec4(float((color>>16u)&255u),float((color>>8u)&255u),float(color&255u),float(color>>24u))/255.0;}\n";
#endif
