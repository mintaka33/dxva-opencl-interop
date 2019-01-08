
constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;

__kernel void scale(read_write image2d_t image) 
{
   int2 coord = (int2)(get_global_id(0), get_global_id(1));

   // the image created from shared d3d11 NV12 surface has image_channel_data_type = CL_UNORM_INT8
   // so need use read_imagef/write_imagef to access
   float4 pixel = read_imagef(image, coord);
   float4 pixel2 = pixel/4;

   write_imagef(image, coord, pixel2);
}
