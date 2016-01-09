#ifndef ___AVCFRAMECHECKER_H___
#define ___AVCFRAMECHECKER_H___

#include "AVCBitsReader.h"

#include <pthread.h>
#include <stdint.h>
#include <list>

/////////////////////////////////////////////////////////////////////////////
////

enum
{
    EXTENDED_SAR            = 255,
    MAX_SLICE_HEADER_SIZE   = 256
};

///< ITU-T Rec. H.264 table 7-1
enum NAL_unit_type
{
    UNKNOWN         = 0,
    SLICE           = 1,   ///< 1 - 5 are VCL NAL units
    SLICE_DPA       = 2,
    SLICE_DPB       = 3,
    SLICE_DPC       = 4,
    SLICE_IDR       = 5,
    SEI             = 6,
    SPS             = 7,
    PPS             = 8,
    AU_DELIMITER    = 9,
    END_SEQUENCE    = 10,
    END_STREAM      = 11,
    FILLER_DATA     = 12,
    SPS_EXT         = 13,
    NALU_prefix     = 14,
    SPS_subset      = 15,
    AUXILIARY_SLICE = 19,
    SLICE_EXTENSION = 20
};

enum SEI_type
{
    SEI_TYPE_PIC_TIMING             = 1,
    SEI_FILLER_PAYLOAD              = 3,
    SEI_TYPE_USER_DATA_UNREGISTERED = 5,
    SEI_TYPE_RECOVERY_POINT         = 6
};

/* slice_type values in the range 5..9 specify, in addition to the
   coding type of the current slice, that all other slices of the
   current coded picture shall have a value of slice_type equal to
   the current value of slice_type or equal to the current value of
   slice_type – 5.
*/
enum SLICE_type
{
    SLICE_P     = 0,
    SLICE_B     = 1,
    SLICE_I     = 2,
    SLICE_SP    = 3,
    SLICE_SI    = 4,
    SLICE_P_a   = 5,
    SLICE_B_a   = 6,
    SLICE_I_a   = 7,
    SLICE_SP_a  = 8,
    SLICE_SI_a  = 9,
    SLICE_UNDEF = 10
};

enum frame_type
{
    FRAME           = 'F',
    FIELD_TOP       = 'T',
    FIELD_BOTTOM    = 'B'
};

struct H264NALU
{
    uint8_t      type;
    const char  *name;
    uint8_t     *data;
    uint32_t     len;
    uint32_t     size;
};

struct H264AU
{
    bool         key;
    frame_type   type;
    const char  *name;
    int32_t      slices;
    std::list<H264NALU *> nalus;
};

/**
 * Parse h264 es streams and drop malformed NALUs
 * or NALUs other than the following types:
 * 'AU_DELIMITER / SPS / PPS / SEI /
 *  SLICE_IDR / SLICE / SLICE_DPA /
 *  SPS_EXT / NALU_prefix / SPS_subset / AUXILIARY_SLICE'.
 */
class AVCFrameChecker
{
public:

    AVCFrameChecker();

    ~AVCFrameChecker();

    uint32_t addBytes(const uint8_t *bytes, const uint32_t byte_count, const uint64_t stream_offset);

    void flush();

    H264AU * getAU();

    void freeAU(H264AU *au);

    bool frameStart() const;

    bool keyFrameStart() const;

    uint32_t pictureWidth() const;

    uint32_t pictureHeight() const;

    uint32_t pictureWidthCropped() const;

    uint32_t pictureHeightCropped() const;

    uint32_t aspectRatio() const;

    double frameRate() const;

    uint64_t frameAUstreamOffset() const;

    uint64_t keyframeAUstreamOffset() const;

    uint64_t SPSstreamOffset() const;

private:

    void reset();

    void set_AU_pending();

    bool is_new_au();

    void alloc_au();

    void alloc_nalu();

    void free_nalu(H264NALU *nalu);

    bool fill_nalu(const uint8_t *data, int32_t len);

    bool fill_nalu(const uint8_t *data, int32_t len, bool found_start_code);

    void finish_nalu();

    void finish_au();

    void write_nalu_header();

    void resetRBSP();

    bool fillRBSP(const uint8_t *byteP, uint32_t byte_count, bool found_start_code);

    void processRBSP(bool rbsp_complete);

    void parse_SPS(uint8_t *sps, uint32_t sps_size, bool& interlaced, int32_t& max_ref_frames);

    bool decode_Header(GetBitContext *gb);

    void decode_SPS(GetBitContext *gb);

    void decode_PPS(GetBitContext * gb);

    void decode_SEI(GetBitContext * gb);

    void vui_parameters(GetBitContext * gb);

    int isKeySlice(uint32_t slice_type);

    bool isSlice(uint8_t nal_type);

    frame_type fieldType() const;

    const uint8_t *find_start_code(const uint8_t * p, const uint8_t *end, uint64_t * state);

    void log(const char *format, ...);

private:

    bool            m_au_pending;
    bool            m_seen_sps;
    bool            m_au_contains_keyframe_message;
    bool            m_is_keyframe;
    bool            m_I_is_keyframe;

    uint64_t        m_sync_accumulator;
    uint8_t        *m_rbsp_buffer;
    uint32_t        m_rbsp_buffer_size;
    uint32_t        m_rbsp_index;
    uint32_t        m_consecutive_zeros;
    bool            m_have_unfinished_NAL;

    int             m_prev_frame_num, m_frame_num;
    uint32_t        m_slice_type;
    int             m_prev_pic_parameter_set_id, m_pic_parameter_set_id;
    int8_t          m_prev_field_pic_flag, m_field_pic_flag;
    int8_t          m_prev_bottom_field_flag, m_bottom_field_flag;
    uint8_t         m_prev_nal_ref_idc, m_nal_ref_idc;
    uint8_t         m_prev_pic_order_cnt_type, m_pic_order_cnt_type;
    int             m_prev_pic_order_cnt_lsb, m_pic_order_cnt_lsb;
    int             m_prev_delta_pic_order_cnt_bottom, m_delta_pic_order_cnt_bottom;
    int             m_prev_delta_pic_order_cnt[2], m_delta_pic_order_cnt[2];
    uint8_t         m_prev_nal_unit_type, m_nal_unit_type;
    uint32_t        m_prev_idr_pic_id, m_idr_pic_id;

    uint32_t        m_log2_max_frame_num, m_log2_max_pic_order_cnt_lsb;
    uint32_t        m_seq_parameter_set_id;

    uint8_t         m_delta_pic_order_always_zero_flag;
    uint8_t         m_separate_colour_plane_flag;
    int8_t          m_frame_mbs_only_flag;
    int8_t          m_pic_order_present_flag;
    int8_t          m_redundant_pic_cnt_present_flag;
    int8_t          m_chroma_format_idc;

    uint32_t        m_num_ref_frames;
    uint32_t        m_redundant_pic_cnt;
    uint32_t        m_pic_width, m_pic_height;
    uint32_t        m_frame_crop_left_offset;
    uint32_t        m_frame_crop_right_offset;
    uint32_t        m_frame_crop_top_offset;
    uint32_t        m_frame_crop_bottom_offset;
    uint8_t         m_aspect_ratio_idc;
    uint32_t        m_sar_width, m_sar_height;
    uint32_t        m_unitsInTick, m_timeScale;
    bool            m_fixedRate;

    uint64_t        m_pkt_offset, m_au_offset, m_frame_start_offset, m_keyframe_start_offset;
    uint64_t        m_sps_offset;
    bool            m_on_frame, m_on_key_frame;

    bool            m_long_start_code;
    H264NALU       *m_nalu;
    H264AU         *m_au;

    std::list<H264AU *> m_aus;

    pthread_mutex_t m_mutex;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif ///< ___AVCFRAMECHECKER_H___
