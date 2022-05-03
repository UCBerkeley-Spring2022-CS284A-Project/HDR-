#include <vector>
#include <string>
#include <limits>
#include <cstdio>
#include <utility> // std::make_pair
#include <stdexcept> // std::runtime_error
#include <opencv2/opencv.hpp> // all opencv header
#include "hdrplus/align.h"
#include "hdrplus/burst.h"
#include "hdrplus/utility.h"

namespace hdrplus
{


// static function only visible within file
static void build_per_grayimg_pyramid( \
    std::vector<cv::Mat>& images_pyramid, \
    const cv::Mat& src_image, \
    const std::vector<int>& inv_scale_factors )
{
    // #ifndef NDEBUG
    // printf("%s::%s build_per_grayimg_pyramid start with scale factor : ", __FILE__, __func__ );
    // for ( int i = 0; i < inv_scale_factors.size(); ++i )
    // {
    //     printf("%d ", inv_scale_factors.at( i ));
    // }
    // printf("\n");
    // #endif

    images_pyramid.resize( inv_scale_factors.size() );

    for ( int i = 0; i < inv_scale_factors.size(); ++i )
    {
        cv::Mat blur_image;
        cv::Mat downsample_image;

        switch ( inv_scale_factors[ i ] )
        {
        case 1:
            images_pyramid[ i ] = src_image.clone();
            // cv::Mat use reference count, will not create deep copy
            downsample_image = src_image;
            break;
        case 2:
            //printf("downsample with gaussian sigma %.2f", inv_scale_factors[ i ] * 0.5 );
            // // Gaussian blur
            cv::GaussianBlur( images_pyramid.at( i-1 ), blur_image, cv::Size(0, 0), inv_scale_factors[ i ] * 0.5 );
        
            // // Downsample
            downsample_image = downsample_nearest_neighbour<uint16_t, 2>( blur_image );
            //downsample_image = downsample_nearest_neighbour<uint16_t, 2>( images_pyramid.at( i-1 ) );

            // Add
            images_pyramid.at( i ) = downsample_image.clone();

            break;
        case 4:
            printf("downsample with gaussian sigma %.2f", inv_scale_factors[ i ] * 0.5 );
            cv::GaussianBlur( images_pyramid.at( i-1 ), blur_image, cv::Size(0, 0), inv_scale_factors[ i ] * 0.5 );
            downsample_image = downsample_nearest_neighbour<uint16_t, 4>( blur_image );
            //downsample_image = downsample_nearest_neighbour<uint16_t, 4>( images_pyramid.at( i-1 ) );
            images_pyramid.at( i ) = downsample_image.clone();
            break;
        default:
            throw std::runtime_error("inv scale factor " + std::to_string( inv_scale_factors[ i ]) + "invalid" );
        } 
    }
}


template< int pyramid_scale_factor_prev_curr, int tilesize_scale_factor_prev_curr >
static void build_upsampled_prev_aligement( \
    std::vector<std::vector<std::pair<int, int>>>& src_alignment, \
    std::vector<std::vector<std::pair<int, int>>>& dst_alignment, 
    int num_tiles_h, int num_tiles_w )
{
    int src_height = src_alignment.size();
    int src_width = src_alignment[ 0 ].size();

    constexpr int repeat_factor = pyramid_scale_factor_prev_curr / tilesize_scale_factor_prev_curr;

    // printf("build_upsampled_prev_aligement with scale factor %d, repeat factor %d, tile size factor %d\n", \
        pyramid_scale_factor_prev_curr, repeat_factor, tilesize_scale_factor_prev_curr );

    int dst_height = src_height * repeat_factor;
    int dst_width = src_width * repeat_factor;

    if ( dst_height > num_tiles_h || dst_width > num_tiles_w )
    {
        throw std::runtime_error("current level number of tiles smaller than upsampled tiles\n");
    }

    // Allocate data for dst_alignment
    // NOTE: number of tiles h, number of tiles w might be different from dst_height, dst_width
    // For tiles between num_tile_h and dst_height, use (0,0)
    dst_alignment.resize( num_tiles_h, std::vector<std::pair<int, int>>( num_tiles_w, std::pair<int, int>(0, 0) ) );

    // Upsample alignment
    for ( int row_i = 0; row_i < src_height; row_i++ )
    {
        for ( int col_i = 0; col_i < src_width; col_i++ )
        {
            // Scale alignment
            std::pair<int, int> align_i = src_alignment[ row_i ][ col_i ];
            align_i.first *= pyramid_scale_factor_prev_curr;
            align_i.second *= pyramid_scale_factor_prev_curr;

            // repeat
            for ( int repeat_row_i = 0; repeat_row_i < repeat_factor; ++repeat_row_i )
            {
                for ( int repeat_col_i = 0; repeat_col_i < repeat_factor; ++repeat_col_i )
                {
                    dst_alignment[ row_i * repeat_factor + repeat_row_i ][ col_i * repeat_factor + repeat_col_i ] = align_i;
                }
            }
        }
    }
}


// Set tilesize as template argument for better compiler optimization result.
template< typename data_type, typename return_type, int tile_size >
static unsigned long long l1_distance( const cv::Mat& img1, const cv::Mat& img2, \
    int img1_tile_row_start_idx, int img1_tile_col_start_idx, \
    int img2_tile_row_start_idx, int img2_tile_col_start_idx )
{
    #define CUSTOME_ABS( x ) ( x ) > 0 ? ( x ) : - ( x )

    const data_type* img1_ptr = (const data_type*)img1.data;
    const data_type* img2_ptr = (const data_type*)img2.data;

    int img1_step = img1.step1();
    int img2_step = img2.step1();

    int img1_width = img1.size().width;
    int img1_height = img1.size().height;

    int img2_width = img2.size().width;
    int img2_height = img2.size().height;

    // Range check for safety
    if ( img1_tile_row_start_idx < 0 || img1_tile_row_start_idx > img1_height - tile_size )
    {
        throw std::runtime_error("l1 distance img1_tile_row_start_idx out of valid range\n");
    }

    if ( img1_tile_col_start_idx < 0 || img1_tile_col_start_idx > img1_width - tile_size )
    {
        throw std::runtime_error("l1 distance img1_tile_col_start_idx out of valid range\n");
    }

    if ( img2_tile_row_start_idx < 0 || img2_tile_row_start_idx > img2_height - tile_size )
    {
        throw std::runtime_error("l1 distance img2_tile_row_start_idx out of valid range\n");
    }

    if ( img2_tile_col_start_idx < 0 || img2_tile_col_start_idx > img2_width - tile_size )
    {
        throw std::runtime_error("l1 distance img2_tile_col_start_idx out of valid range\n");
    }

    return_type sum(0);
    // TODO: add pragma unroll here
    for ( int row_i = 0; row_i < tile_size; ++row_i )
    {
        const data_type* img1_ptr_row_i = img1_ptr + (img1_tile_row_start_idx + row_i) * img1_step + img1_tile_col_start_idx;
        const data_type* img2_ptr_row_i = img2_ptr + (img2_tile_row_start_idx + row_i) * img2_step + img2_tile_col_start_idx;

        for ( int col_i = 0; col_i < tile_size; ++col_i )
        {
            data_type l1 = CUSTOME_ABS( img1_ptr_row_i[ col_i ] - img2_ptr_row_i[ col_i ] );
            sum += l1;
        }
    }

    #undef CUSTOME_ABS

    return sum;
}


template< typename data_type, typename return_type, int tile_size >
static return_type l2_distance( const cv::Mat& img1, const cv::Mat& img2, \
    int img1_tile_row_start_idx, int img1_tile_col_start_idx, \
    int img2_tile_row_start_idx, int img2_tile_col_start_idx )
{
    #define CUSTOME_ABS( x ) ( x ) > 0 ? ( x ) : - ( x )

    const data_type* img1_ptr = (const data_type*)img1.data;
    const data_type* img2_ptr = (const data_type*)img2.data;

    int img1_step = img1.step1();
    int img2_step = img2.step1();

    int img1_width = img1.size().width;
    int img1_height = img1.size().height;

    int img2_width = img2.size().width;
    int img2_height = img2.size().height;

    // Range check for safety
    if ( img1_tile_row_start_idx < 0 || img1_tile_row_start_idx > img1_height - tile_size )
    {
        throw std::runtime_error("l1 distance img1_tile_row_start_idx out of valid range\n");
    }

    if ( img1_tile_col_start_idx < 0 || img1_tile_col_start_idx > img1_width - tile_size )
    {
        throw std::runtime_error("l1 distance img1_tile_col_start_idx out of valid range\n");
    }

    if ( img2_tile_row_start_idx < 0 || img2_tile_row_start_idx > img2_height - tile_size )
    {
        throw std::runtime_error("l1 distance img2_tile_row_start_idx out of valid range\n");
    }

    if ( img2_tile_col_start_idx < 0 || img2_tile_col_start_idx > img2_width - tile_size )
    {
        throw std::runtime_error("l1 distance img2_tile_col_start_idx out of valid range\n");
    }

    // printf("Search two tile with ref : \n");
    // print_tile<data_type>( img1, tile_size, img1_tile_row_start_idx, img1_tile_col_start_idx );
    // printf("Search two tile with alt :\n");
    // print_tile<data_type>( img2, tile_size, img2_tile_row_start_idx, img2_tile_col_start_idx );

    return_type sum(0);
    // TODO: add pragma unroll here
    for ( int row_i = 0; row_i < tile_size; ++row_i )
    {
        const data_type* img1_ptr_row_i = img1_ptr + (img1_tile_row_start_idx + row_i) * img1_step + img1_tile_col_start_idx;
        const data_type* img2_ptr_row_i = img2_ptr + (img2_tile_row_start_idx + row_i) * img2_step + img2_tile_col_start_idx;

        for ( int col_i = 0; col_i < tile_size; ++col_i )
        {
            data_type l1 = CUSTOME_ABS( img1_ptr_row_i[ col_i ] - img2_ptr_row_i[ col_i ] );
            sum += ( l1 * l1 );
        }
    }

    #undef CUSTOME_ABS

    return sum;
}


void align_image_level( \
    const cv::Mat& ref_img, \
    const cv::Mat& alt_img, \
    std::vector<std::vector<std::pair<int, int>>>& prev_aligement, \
    std::vector<std::vector<std::pair<int, int>>>& curr_alignment, \
    int scale_factor_prev_curr, \
    int curr_tile_size, \
    int prev_tile_size, \
    int search_radiou, \
    int distance_type )
{
    // Every align image level share the same distance function. 
    // Use function ptr to reduce if else overhead inside for loop
    unsigned long long (*distance_func_ptr)(const cv::Mat&, const cv::Mat&, int, int, int, int) = nullptr;

    if ( distance_type == 1 ) // l1 distance
    {
        if ( curr_tile_size == 8 )
        {
            distance_func_ptr = &l1_distance<uint16_t, unsigned long long, 8>;
        }
        else if ( curr_tile_size == 16 )
        {
            distance_func_ptr = &l1_distance<uint16_t, unsigned long long, 16>;
        }
    }
    else if ( distance_type == 2 ) // l2 distance
    {
        if ( curr_tile_size == 8 )
        {
            distance_func_ptr = &l2_distance<uint16_t, unsigned long long, 8>;
        }
        else if ( curr_tile_size == 16 )
        {
            distance_func_ptr = &l2_distance<uint16_t, unsigned long long, 16>;
        }
    }

    // Every level share the same upsample function
    void (*upsample_alignment_func_ptr)(std::vector<std::vector<std::pair<int, int>>>&, std::vector<std::vector<std::pair<int, int>>>&, int, int) = nullptr;
    if ( scale_factor_prev_curr == 2 )
    {
        if ( curr_tile_size / prev_tile_size == 2 )
        {
            upsample_alignment_func_ptr = &build_upsampled_prev_aligement<2, 2>;
        }
        else if ( curr_tile_size / prev_tile_size == 1 )
        {
            upsample_alignment_func_ptr = &build_upsampled_prev_aligement<2, 1>;
        }
        else
        {
            throw std::runtime_error("Something wrong with upsampling function setting\n");
        }
    }
    else if ( scale_factor_prev_curr == 4 )
    {
        if ( curr_tile_size / prev_tile_size == 2 )
        {
            upsample_alignment_func_ptr = &build_upsampled_prev_aligement<4, 2>;
        }
        else if ( curr_tile_size / prev_tile_size == 1 )
        {
            upsample_alignment_func_ptr = &build_upsampled_prev_aligement<4, 1>;
        }
        else
        {
            throw std::runtime_error("Something wrong with upsampling function setting\n");
        }
    }

    int num_tiles_h = ref_img.size().height / (curr_tile_size / 2) - 1;
    int num_tiles_w = ref_img.size().width / (curr_tile_size / 2 ) - 1;

    /* Upsample pervious layer alignment */
    std::vector<std::vector<std::pair<int, int>>> upsampled_prev_aligement;

    // Coarsest level
    // prev_alignment is invalid / empty, construct alignment as (0,0)
    if ( prev_tile_size == -1 )
    {
        upsampled_prev_aligement.resize( num_tiles_h, \
            std::vector<std::pair<int, int>>( num_tiles_w, std::pair<int, int>(0, 0) ) );
    }
    // Upsample previous level alignment 
    else
    {
        upsample_alignment_func_ptr( prev_aligement, upsampled_prev_aligement, num_tiles_h, num_tiles_w );

        // printf("\n!!!!!Upsampled previous alignment\n");
        // for ( int tile_row = 0; tile_row < upsampled_prev_aligement.size(); tile_row++ )
        // {
        //     for ( int tile_col = 0; tile_col < upsampled_prev_aligement.at(0).size(); tile_col++ )
        //     {
        //         const auto tile_start = upsampled_prev_aligement.at( tile_row ).at( tile_col );
        //         printf("up tile (%d, %d) -> start idx (%d, %d)\n", \
        //             tile_row, tile_col, tile_start.first, tile_start.second);
        //     }
        // }

    }

    #ifndef NDEBUG
    printf("%s::%s start: \n", __FILE__, __func__ );
    printf("    scale_factor_prev_curr %d, tile_size %d, prev_tile_size %d, search_radiou %d, distance L%d, \n", \
        scale_factor_prev_curr, curr_tile_size, prev_tile_size, search_radiou, distance_type );
    printf("    ref img size h=%d w=%d, alt img size h=%d w=%d, \n", \
        ref_img.size().height, ref_img.size().width, alt_img.size().height, alt_img.size().width );
    printf("    num tile h (upsampled) %d, num tile w (upsampled) %d\n", num_tiles_h, num_tiles_w);
    #endif

    // allocate memory for current alignmenr
    curr_alignment.resize( num_tiles_h, std::vector<std::pair<int, int>>( num_tiles_w, std::pair<int, int>(0, 0) ) );

    /* Pad alternative image */
    cv::Mat alt_img_pad;
    cv::copyMakeBorder( alt_img, \
                        alt_img_pad, \
                        search_radiou, search_radiou, search_radiou, search_radiou, \
                        cv::BORDER_CONSTANT, cv::Scalar( UINT_LEAST16_MAX ) );

    // printf("Reference image h=%d, w=%d: \n", ref_img.size().height, ref_img.size().width );
    // print_img<uint16_t>( ref_img );

    // printf("Alter image pad h=%d, w=%d: \n", alt_img_pad.size().height, alt_img_pad.size().width );
    // print_img<uint16_t>( alt_img_pad );

    //printf("!! enlarged tile size %d\n", curr_tile_size + 2 * search_radiou );

    int alt_tile_row_idx_max = alt_img_pad.size().height - ( curr_tile_size + 2 * search_radiou );
    int alt_tile_col_idx_max = alt_img_pad.size().width  - ( curr_tile_size + 2 * search_radiou );

    // TODO delete below distance vector, this is for debug only
    std::vector<std::vector<uint16_t>> distances( num_tiles_h, std::vector<uint16_t>( num_tiles_w, 0 ));

    /* Iterate through all reference tile & compute distance */
    for ( int ref_tile_row_i = 0; ref_tile_row_i < num_tiles_h; ref_tile_row_i++ )
    {
        for ( int ref_tile_col_i = 0; ref_tile_col_i < num_tiles_w; ref_tile_col_i++ )
        {
            // Upper left index of reference tile
            int ref_tile_row_start_idx_i = ref_tile_row_i * curr_tile_size / 2;
            int ref_tile_col_start_idx_i = ref_tile_col_i * curr_tile_size / 2;

            //printf("\nRef img tile [%d, %d] -> start idx [%d, %d] (row, col)\n", \
                ref_tile_row_i, ref_tile_col_i, ref_tile_row_start_idx_i, ref_tile_col_start_idx_i );
            // printf("\nRef img tile [%d, %d]\n", ref_tile_row_i, ref_tile_col_i );
            // print_tile<uint16_t>( ref_img, curr_tile_size, ref_tile_row_start_idx_i, ref_tile_col_start_idx_i );

            // Upsampled alignment at this tile
            // Alignment are relative displacement in pixel value
            int prev_alignment_row_i = upsampled_prev_aligement.at( ref_tile_row_i ).at( ref_tile_col_i ).first;
            int prev_alignment_col_i = upsampled_prev_aligement.at( ref_tile_row_i ).at( ref_tile_col_i ).second;

            // Alternative image tile start idx
            int alt_tile_row_start_idx_i = ref_tile_row_start_idx_i + prev_alignment_row_i;
            int alt_tile_col_start_idx_i = ref_tile_col_start_idx_i + prev_alignment_col_i;

            // Ensure alternative image tile within range
            if ( alt_tile_row_start_idx_i < 0 )
                alt_tile_row_start_idx_i = 0;
            if ( alt_tile_col_start_idx_i < 0 )
                alt_tile_col_start_idx_i = 0;
            if ( alt_tile_row_start_idx_i > alt_tile_row_idx_max )
            {
                int before = alt_tile_row_start_idx_i;
                alt_tile_row_start_idx_i = alt_tile_row_idx_max;
                // printf("@@ change start x from %d to %d\n", before, alt_tile_row_idx_max);
            }
            if ( alt_tile_col_start_idx_i > alt_tile_col_idx_max )
            {
                int before = alt_tile_col_start_idx_i;
                alt_tile_col_start_idx_i = alt_tile_col_idx_max;
                // printf("@@ change start y from %d to %d\n", before, alt_tile_col_idx_max );
            }

            // Because alternative image is padded with search radious. 
            // Using same coordinate with reference image will automatically considered search radious * 2
            //printf("Alt image tile [%d, %d]-> start idx [%d, %d]\n", \
                ref_tile_row_i, ref_tile_col_i, alt_tile_row_start_idx_i, alt_tile_col_start_idx_i );
            // printf("\nAlt image tile [%d, %d]\n", ref_tile_row_i, ref_tile_col_i );
            // print_tile<uint16_t>( alt_img_pad, curr_tile_size + 2 * search_radiou, alt_tile_row_start_idx_i, alt_tile_col_start_idx_i );

            // Search based on L1/L2 distance
            unsigned long long min_distance_i = ULONG_LONG_MAX;
            int min_distance_row_i = -1;
            int min_distance_col_i = -1;
            for ( int search_row_j = 0; search_row_j < ( search_radiou * 2 + 1 ); search_row_j++ )
            {
                for ( int search_col_j = 0; search_col_j < ( search_radiou * 2 + 1 ); search_col_j++ )
                {
                    //printf("\n--->tile at [%d, %d] search (%d, %d)\n", \
                        ref_tile_row_i, ref_tile_col_i, search_row_j - search_radiou, search_col_j - search_radiou );

                    // TODO: currently distance is incorrect
                    unsigned long long distance_j = distance_func_ptr( ref_img, alt_img_pad, \
                        ref_tile_row_start_idx_i, ref_tile_col_start_idx_i, \
                        alt_tile_row_start_idx_i + search_row_j, alt_tile_col_start_idx_i + search_col_j );

                    //printf("<---tile at [%d, %d] search (%d, %d), new dis %llu, old dis %llu\n", \
                        ref_tile_row_i, ref_tile_col_i, search_row_j - search_radiou, search_col_j - search_radiou, distance_j, min_distance_i );

                    // If this is smaller distance
                    if ( distance_j < min_distance_i )
                    {
                        min_distance_i = distance_j;
                        min_distance_col_i = search_col_j;
                        min_distance_row_i = search_row_j;
                    }

                    // If same value, choose the one closer to the original tile location
                    // if ( distance_j == min_distance_i && min_distance_row_i != -1 && min_distance_col_i != -1 )
                    // {
                    //     int prev_distance_row_2_ref = min_distance_row_i - search_radiou;
                    //     int prev_distance_col_2_ref = min_distance_col_i - search_radiou;
                    //     int curr_distance_row_2_ref = search_row_j - search_radiou;
                    //     int curr_distance_col_2_ref = search_col_j - search_radiou;

                    //     int prev_distance_2_ref_sqr = prev_distance_row_2_ref * prev_distance_row_2_ref + prev_distance_col_2_ref * prev_distance_col_2_ref;
                    //     int curr_distance_2_ref_sqr = curr_distance_row_2_ref * curr_distance_row_2_ref + curr_distance_col_2_ref * curr_distance_col_2_ref;

                    //     // previous min distance idx is farther away from ref tile start location
                    //     if ( prev_distance_2_ref_sqr > curr_distance_2_ref_sqr )
                    //     {
                    //         // printf("@@@ Same distance %d, choose closer one (%d, %d) instead of (%d, %d)\n", \
                    //             distance_j, search_row_j, search_col_j, min_distance_row_i, min_distance_col_i);
                    //         min_distance_col_i = search_col_j;
                    //         min_distance_row_i = search_row_j;
                    //     }
                    // }
                }
            }

            //printf("tile at (%d, %d) alignment (%d, %d)\n", \
                ref_tile_row_i, ref_tile_col_i, min_distance_row_i, min_distance_col_i );

            int alignment_row_i = prev_alignment_row_i + min_distance_row_i - search_radiou;
            int alignment_col_i = prev_alignment_col_i + min_distance_col_i - search_radiou;

            std::pair<int, int> alignment_i( alignment_row_i, alignment_col_i );

            // Add min_distance_i's corresbonding idx as min
            curr_alignment.at( ref_tile_row_i ).at( ref_tile_col_i ) = alignment_i;
            distances.at( ref_tile_row_i ).at( ref_tile_col_i ) = min_distance_i;
        }
    }

    // printf("\n!!!!!Min distance for each tile \n");
    // for ( int tile_row = 0; tile_row < num_tiles_h; tile_row++ )
    // {
    //     for ( int tile_col = 0; tile_col < num_tiles_w; ++tile_col )
    //     {
    //         printf("tile (%d, %d) distance %u\n", \
    //             tile_row, tile_col, distances.at( tile_row).at(tile_col ) );
    //     }
    // }

    // printf("\n!!!!!Alignment at current level\n");
    // for ( int tile_row = 0; tile_row < num_tiles_h; tile_row++ )
    // {
    //     for ( int tile_col = 0; tile_col < num_tiles_w; tile_col++ )
    //     {
    //         const auto tile_start = curr_alignment.at( tile_row ).at( tile_col );
    //         printf("tile (%d, %d) -> start idx (%d, %d)\n", \
    //             tile_row, tile_col, tile_start.first, tile_start.second);
    //     }
    // }

}


static void build_per_pyramid_reftiles_start( \
    std::vector<std::vector<std::vector<std::pair<int, int>>>>& per_pyramid_reftiles_start, \
    const std::vector<std::vector<cv::Mat>>& per_grayimg_pyramid, \
    const std::vector<int>& grayimg_tile_sizes )
{
    per_pyramid_reftiles_start.resize( per_grayimg_pyramid.at(0).size() );

    // Every image pyramid level
    for ( int level_i = 0; level_i < per_grayimg_pyramid.at(0).size(); level_i++ )
    {
        int level_i_img_h = per_grayimg_pyramid.at(0).at( level_i ).size().height;
        int level_i_img_w = per_grayimg_pyramid.at(0).at( level_i ).size().width;

        int level_i_tile_size = grayimg_tile_sizes.at( level_i );

        int num_tiles_h = level_i_img_h / (level_i_tile_size / 2) - 1;
        int num_tiles_w = level_i_img_w / (level_i_tile_size / 2) - 1;

        // Allocate memory
        per_pyramid_reftiles_start.at( level_i ).resize( num_tiles_h, std::vector<std::pair<int, int>>( num_tiles_w ) );

        for ( int tile_col_i = 0; tile_col_i < num_tiles_h; tile_col_i++ )
        {
            for ( int tile_row_j = 0; tile_row_j < num_tiles_w; tile_row_j++ )
            {
                per_pyramid_reftiles_start.at( level_i ).at( tile_col_i ).at( tile_row_j ) \
                    = std::make_pair<int, int>( tile_col_i * level_i_tile_size, tile_row_j * level_i_tile_size );
            }
        }
    }
}


void align::process( const hdrplus::burst& burst_images, \
                     std::vector<std::vector<std::vector<std::pair<int, int>>>>& images_alignment )
{
    #ifndef NDEBUG
    printf("%s::%s align::process start\n", __FILE__, __func__ ); fflush(stdout);
    #endif

    images_alignment.clear();
    images_alignment.resize( burst_images.num_images );

    // image pyramid per image, per pyramid level
    std::vector<std::vector<cv::Mat>> per_grayimg_pyramid;

	// printf("!!!!! ref bayer padded\n");
    // print_img<uint16_t>( burst_images.bayer_images_pad.at( burst_images.reference_image_idx) );
    // exit(1);

	// printf("!!!!! ref gray padded\n");
    // print_img<uint16_t>( burst_images.grayscale_images_pad.at( burst_images.reference_image_idx) );
    // exit(1);

    per_grayimg_pyramid.resize( burst_images.num_images );
    for ( int img_idx = 0; img_idx < burst_images.num_images; ++img_idx )
    {
        // per_grayimg_pyramid[ img_idx ][ 0 ] is the original image
        // per_grayimg_pyramid[ img_idx ][ 3 ] is the coarsest image
        build_per_grayimg_pyramid( per_grayimg_pyramid.at( img_idx ), \
                                   burst_images.grayscale_images_pad.at( img_idx ), \
                                   this->inv_scale_factors );
    }

    // #ifndef NDEBUG
    // printf("%s::%s build image pyramid of size : ", __FILE__, __func__ );
    // for ( int level_i = 0; level_i < num_levels; ++level_i )
    // {
    //     printf("(%d, %d) ", per_grayimg_pyramid[ 0 ][ level_i ].size().height,
    //                         per_grayimg_pyramid[ 0 ][ level_i ].size().width );
    // }
    // printf("\n"); fflush(stdout);
    // #endif

    // print image pyramid
    // for ( int level_i; level_i < num_levels; ++level_i )
    // {
    //     printf("\n\n!!!!! ref gray pyramid level %d img : \n" , level_i );
    //     print_img<uint16_t>( per_grayimg_pyramid[ burst_images.reference_image_idx ][ level_i ] );
    // }
    // exit(-1);

    // Align every image
    const std::vector<cv::Mat>& ref_grayimg_pyramid = per_grayimg_pyramid[ burst_images.reference_image_idx ];
    for ( int img_idx = 0; img_idx < burst_images.num_images; ++img_idx )
    {
        // Do not align with reference image
        if ( img_idx == burst_images.reference_image_idx )
            continue;

        const std::vector<cv::Mat>& alt_grayimg_pyramid = per_grayimg_pyramid[ img_idx ];

        // Align every level from coarse to grain
        // level 0 : finest level, the original image
        // level 3 : coarsest level
        std::vector<std::vector<std::pair<int, int>>> curr_alignment;
        std::vector<std::vector<std::pair<int, int>>> prev_alignment;
        for ( int level_i = num_levels - 1; level_i >= 0; level_i-- ) // 3,2,1,0
        {
            // make curr alignment as previous alignment
            prev_alignment.swap( curr_alignment );
            curr_alignment.clear();

            // printf("\n\n########################align level %d\n", level_i );
            align_image_level(
                ref_grayimg_pyramid[ level_i ],    // reference image at current level
                alt_grayimg_pyramid[ level_i ],    // alternative image at current level
                prev_alignment,                    // previous layer alignment
                curr_alignment,                    // current layer alignment
                ( level_i == ( num_levels - 1 ) ? -1 : inv_scale_factors[ level_i + 1 ] ), // scale factor between previous layer and current layer. -1 if current layer is the coarsest layer, [-1, 4, 4, 2]
                grayimg_tile_sizes[ level_i ],     // current level tile size
                ( level_i == ( num_levels - 1 ) ? -1 : grayimg_tile_sizes[ level_i + 1 ] ), // previous level tile size
                grayimg_search_radious[ level_i ], // search radious
                distances[ level_i ] );            // L1/L2 distance
           
            // printf("@@@Alignment at level %d is h=%d, w=%d", level_i, curr_alignment.size(), curr_alignment.at(0).size() );

            // Stop at second iteration
            // if ( level_i == num_levels - 3 )
            //     break;

        } // for pyramid level

        // Alignment at grayscale image
        images_alignment.at( img_idx ).swap( curr_alignment );

        // printf("\n!!!!!Alternative Image Alignment\n");
        // for ( int tile_row = 0; tile_row < images_alignment.at( img_idx ).size(); tile_row++ )
        // {
        //     for ( int tile_col = 0; tile_col < images_alignment.at( img_idx ).at(0).size(); tile_col++ )
        //     {
        //         const auto tile_start = images_alignment.at( img_idx ).at( tile_row ).at( tile_col );
        //         printf("tile (%d, %d) -> start idx (%d, %d)\n", \
        //             tile_row, tile_col, tile_start.first, tile_start.second);
        //     }
        // }

    } // for alternative image

}

} // namespace hdrplus
