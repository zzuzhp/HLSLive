#include "AVCFrameChecker.h"

#include <windows.h>
#include <cassert>
#include <string>
#include <cstdio>
#include <math.h>
#include <new>

static const float eps  = 1E-5;
static const uint32_t naluinitsize = 16000; ///< 16k initial buffer

static const char *nalu_names[] =
{
    "UNKNOWN",          ///< 0: UNKNOWN
    "SLICE",            ///< 1: SLICE
    "SLICE_DPA",        ///< 2: SLICE_DPA
    "SLICE_DPB",        ///< 3: SLICE_DPB
    "SLICE_DPC",        ///< 4: SLICE_DPC
    "SLICE_IDR",        ///< 5: SLICE_IDR
    "SEI",              ///< 6: SEI
    "SPS",              ///< 7: SPS
    "PPS",              ///< 8: PPS
    "AU_DELIMITER",     ///< 9: AU_DELIMITER
    "END_SEQUENCE",     ///< 10:END_SEQUENCE
    "END_STREAM",       ///< 11:END_STREAM
    "FILLER_DATA",      ///< 12:FILLER_DATA
    "SPS_EXT",          ///< 13:SPS_EXT
    "OTHER",            ///< 14:NALU_prefix
    "OTHER",            ///< 15:SPS_subset
    "Reserved",         ///< 16:
    "Reserved",         ///< 17:
    "Reserved",         ///< 18:
    "AUXILIARY_SLICE",  ///< 19:AUXILIARY_SLICE
    "SLICE_EXTENSION",  ///< 20:SLICE_EXTENSION
    "Reserved",         ///< 21:
    "Reserved",         ///< 22:
    "Reserved",         ///< 23:
    "Unspecified",      ///< 24:
    "Unspecified",      ///< 25:
    "Unspecified",      ///< 26:
    "Unspecified",      ///< 27:
    "Unspecified",      ///< 28:
    "Unspecified",      ///< 29:
    "Unspecified",      ///< 30:
    "Unspecified"       ///< 31:
};

///////////////////////SLICE_EXTENSION///////////////////////////////////////////////////
////

/*
    Most of the comments below were cut&paste from ITU-T Rec. H.264
    as found here:  http://www.itu.int/rec/T-REC-H.264/e
*/

/* Useful definitions:

   * access unit: A set of NAL units always containing exactly one
   primary coded picture. In addition to the primary coded picture, an
   access unit may also contain one or more redundant coded pictures
   or other NAL units not containing slices or slice data partitions
   of a coded picture. The decoding of an access unit always results
   in a decoded picture.

   * instantaneous decoding refresh (IDR) access unit: An access unit in
   which the primary coded picture is an IDR picture.

   * instantaneous decoding refresh (IDR) picture: A coded picture
   containing only slices with I or SI slice types that causes the
   decoding process to mark all reference pictures as "unused for
   reference" immediately after decoding the IDR picture. After the
   decoding of an IDR picture all following coded pictures in decoding
   order can be decoded without inter prediction from any picture
   decoded prior to the IDR picture. The first picture of each coded
   video sequence is an IDR picture.

   * NAL unit: A syntax structure containing an indication of the type
   of data to follow and bytes containing that data in the form of an
   RBSP interspersed as necessary with emulation prevention bytes.

   * raw byte sequence payload (RBSP): A syntax structure containing an
   integer number of bytes that is encapsulated in a NAL unit. An RBSP
   is either empty or has the form of a string of data bits containing
   syntax elements followed by an RBSP stop bit and followed by zero
   or more subsequent bits equal to 0.

   * raw byte sequence payload (RBSP) stop bit: A bit equal to 1 present
   within a raw byte sequence payload (RBSP) after a string of data
   bits. The location of the end of the string of data bits within an
   RBSP can be identified by searching from the end of the RBSP for
   the RBSP stop bit, which is the last non-zero bit in the RBSP.

   * parity: The parity of a field can be top or bottom.

   * picture: A collective term for a field or a frame.

   * picture parameter set: A syntax structure containing syntax
    elements that apply to zero or more entire coded pictures as
    determined by the pic_parameter_set_id syntax element found in each
    slice header.

   * primary coded picture: The coded representation of a picture to be
    used by the decoding process for a bitstream conforming to this
    Recommendation | International Standard. The primary coded picture
    contains all macroblocks of the picture. The only pictures that
    have a normative effect on the decoding process are primary coded
    pictures. See also redundant coded picture.

   * VCL: Video Coding Layer

    - The VCL is specified to efficiently represent the content of the
    video data. The NAL is specified to format that data and provide
    header information in a manner appropriate for conveyance on a
    variety of communication channels or storage media. All data are
    contained in NAL units, each of which contains an integer number of
    bytes. A NAL unit specifies a generic format for use in both
    packet-oriented and bitstream systems. The format of NAL units for
    both packet-oriented transport and byte stream is identical except
    that each NAL unit can be preceded by a start code prefix and extra
    padding bytes in the byte stream format.
*/
AVCFrameChecker::AVCFrameChecker()
{
    pthread_mutex_init(&m_mutex, NULL);

    m_rbsp_buffer_size = 188 * 2;
    m_rbsp_buffer      = new uint8_t[m_rbsp_buffer_size];

    if (m_rbsp_buffer == 0)
    {
        m_rbsp_buffer_size = 0;
    }

    reset();

    m_I_is_keyframe                = true;
    m_au_contains_keyframe_message = false;

    alloc_nalu();

    alloc_au();
}

AVCFrameChecker::~AVCFrameChecker()
{
    pthread_mutex_lock(&m_mutex);

    while (!m_aus.empty())
    {
        H264AU *au = m_aus.front();
        m_aus.pop_front();

        freeAU(au);
    }

    pthread_mutex_unlock(&m_mutex);

    freeAU(m_au);

    free_nalu(m_nalu);

    delete[] m_rbsp_buffer;

    pthread_mutex_destroy(&m_mutex);
}

void
AVCFrameChecker::reset()
{
    m_seen_sps                          = false;
    m_on_frame                          = false;
    m_au_pending                        = false;
    m_is_keyframe                       = false;
    m_on_key_frame                      = false;
    m_long_start_code                   = false;

    m_au                                = 0;
    m_nalu                              = 0;
    m_pic_width                         = 0;
    m_pic_height                        = 0;
    m_sar_width                         = 0;
    m_sar_height                        = 0;
    m_timeScale                         = 0;
    m_fixedRate                         = 0;
    m_au_offset                         = 0;
    m_sps_offset                        = 0;
    m_pkt_offset                        = 0;
    m_unitsInTick                       = 0;
    m_num_ref_frames                    = 0;
    m_pic_order_cnt_lsb                 = 0;
    m_pic_order_cnt_type                = 0;
    m_log2_max_frame_num                = 0;
    m_prev_pic_order_cnt_lsb            = 0;
    m_prev_pic_order_cnt_type           = 0;
    m_frame_crop_top_offset             = 0;
    m_frame_crop_left_offset            = 0;
    m_frame_crop_right_offset           = 0;
    m_frame_crop_bottom_offset          = 0;
    m_delta_pic_order_cnt_bottom        = 0;
    m_delta_pic_order_cnt[0]            = 0;
    m_delta_pic_order_cnt[1]            = 0;
    m_prev_delta_pic_order_cnt[0]       = 0;
    m_prev_delta_pic_order_cnt[1]       = 0;
    m_prev_delta_pic_order_cnt_bottom   = 0;
    m_redundant_pic_cnt_present_flag    = 0;
    m_redundant_pic_cnt                 = 0;
    m_aspect_ratio_idc                  = 0;
    m_frame_start_offset                = 0;
    m_keyframe_start_offset             = 0;
    m_log2_max_pic_order_cnt_lsb        = 0;
    m_seq_parameter_set_id              = 0;
    m_delta_pic_order_always_zero_flag  = 0;
    m_separate_colour_plane_flag        = 0;

    m_frame_num                         = -1;
    m_prev_frame_num                    = -1;
    m_pic_parameter_set_id              = -1;
    m_prev_pic_parameter_set_id         = -1;
    m_prev_field_pic_flag               = -1;
    m_field_pic_flag                    = -1;
    m_bottom_field_flag                 = -1;
    m_prev_bottom_field_flag            = -1;
    m_chroma_format_idc                 = 1;
    m_frame_mbs_only_flag               = -1;
    m_pic_order_present_flag            = -1;

    m_slice_type                        = SLICE_UNDEF;
    m_prev_nal_ref_idc                  = m_nal_ref_idc = 111;  //  != [0|1|2|3]
    m_prev_nal_unit_type                = m_nal_unit_type = UNKNOWN;

    ///< The value of idr_pic_id shall be in the range of 0 to 65535, inclusive.
    m_prev_idr_pic_id                   = m_idr_pic_id = 65536;
    m_sync_accumulator                  = 0xffffffffffffffff;

    resetRBSP();
}

void
AVCFrameChecker::alloc_au()
{
    ///< create new AU
    m_au = new(std::nothrow) H264AU;

    if (m_au)
    {
        m_au->slices = 0;
    }
}

void
AVCFrameChecker::alloc_nalu()
{
    ///< create new NALU
    m_nalu = new H264NALU;

    if (m_nalu)
    {
        m_nalu->len  = 0;
        m_nalu->data = new(std::nothrow) uint8_t[naluinitsize];

        assert(m_nalu->data);
        if (!m_nalu->data)
        {
            delete m_nalu;
            m_nalu = 0;
        }

        m_nalu->size = naluinitsize;
    }
}

void
AVCFrameChecker::freeAU(H264AU *au)
{
    if (au)
    {
        while (!au->nalus.empty())
        {
            H264NALU *nalu = au->nalus.front();
            au->nalus.pop_front();

            free_nalu(nalu);
        }

        delete au;
    }
}

void
AVCFrameChecker::free_nalu(H264NALU *nalu)
{
    if (nalu)
    {
        delete[] nalu->data;
        delete nalu;
    }
}

bool
AVCFrameChecker::fill_nalu(const uint8_t *data, int32_t len)
{
    if (len > 0)
    {
        if (len + m_nalu->len > m_nalu->size)
        {
            int size = len + m_nalu->len + 10000; ///< 10k more

            uint8_t *nalubuf = new(std::nothrow) uint8_t[size];
            if (!nalubuf)
            {
                assert(0);
                log("H264Parser::fillRBSP: FAILED to allocate NALU buffer!");

                return false;
            }

            m_nalu->size = size;

            memcpy(nalubuf, m_nalu->data, m_nalu->len);

            delete[] m_nalu->data;

            m_nalu->data = nalubuf;
        }

        memcpy(m_nalu->data + m_nalu->len, data, len);
    }

    m_nalu->len += len; ///< 'len' could be negative

    return true;
}

bool
AVCFrameChecker::fill_nalu(const uint8_t * data, int32_t len, bool found_start_code)
{
    if (found_start_code)
    {
        len -= ((m_sync_accumulator & 0x000000ff00000000) == 0 ? 5 : 4);
    }

    return fill_nalu(data, len);
}

void
AVCFrameChecker::finish_nalu()
{
    if (m_nalu->len)
    {
        if (isSlice(m_nalu->type))
        {
            m_au->key  = (m_nalu->type & 0x1f) == 5;
            m_au->name = m_au->key ? "IDR" : "non-IDR";
            ++m_au->slices;
        }

        m_au->nalus.push_back(m_nalu);
        alloc_nalu();
    }
}

void
AVCFrameChecker::finish_au()
{
    pthread_mutex_lock(&m_mutex);

    if (m_au->nalus.size())
    {
        m_aus.push_back(m_au);
        alloc_au();
    }

    pthread_mutex_unlock(&m_mutex);
}

void
AVCFrameChecker::write_nalu_header()
{
    uint8_t head[5] = {'\x0', '\x0', '\x0', '\x1', m_sync_accumulator & 0xff};

    m_nalu->type = m_nal_unit_type;
    m_nalu->name = nalu_names[m_nal_unit_type];

    fill_nalu(&head[m_long_start_code ? 0 : 1], m_long_start_code ? 5 : 4);
}

bool
AVCFrameChecker::is_new_au()
{
    /* An access unit consists of one primary coded picture, zero or more
       corresponding redundant coded pictures, and zero or more non-VCL NAL
       units. The association of VCL NAL units to primary or redundant coded
       pictures is described in subclause 7.4.1.2.5.

       The first access unit in the bitstream starts with the first NAL unit
       of the bitstream.

       The first of any of the following NAL units after the last VCL NAL
       unit of a primary coded picture specifies the start of a new access
       unit.

       -   access unit delimiter NAL unit (when present)
       -   sequence parameter set NAL unit (when present)
       -   picture parameter set NAL unit (when present)
       -   SEI NAL unit (when present)
       -   NAL units with nal_unit_type in the range of 14 to 18, inclusive
       -   first VCL NAL unit of a primary coded picture (always present)
    */

    /* 7.4.1.2.4 Detection of the first VCL NAL unit of a primary coded
       picture This subclause specifies constraints on VCL NAL unit syntax
       that are sufficient to enable the detection of the first VCL NAL unit
       of each primary coded picture.

       Any coded slice NAL unit or coded slice data partition A NAL unit of
       the primary coded picture of the current access unit shall be
       different from any coded slice NAL unit or coded slice data partition
       A NAL unit of the primary coded picture of the previous access unit in
       one or more of the following ways.

       - frame_num differs in value. The value of frame_num used to
         test this condition is the value of frame_num that appears in
         the syntax of the slice header, regardless of whether that value
         is inferred to have been equal to 0 for subsequent use in the
         decoding process due to the presence of
         memory_management_control_operation equal to 5.
           Note: If the current picture is an IDR picture FrameNum and
           PrevRefFrameNum are set equal to 0.
       - pic_parameter_set_id differs in value.
       - field_pic_flag differs in value.
       - bottom_field_flag is present in both and differs in value.
       - nal_ref_idc differs in value with one of the nal_ref_idc
         values being equal to 0.
       - pic_order_cnt_type is equal to 0 for both and either
         pic_order_cnt_lsb differs in value, or delta_pic_order_cnt_bottom
         differs in value.
       - pic_order_cnt_type is equal to 1 for both and either
         delta_pic_order_cnt[0] differs in value, or
         delta_pic_order_cnt[1] differs in value.
       - nal_unit_type differs in value with one of the nal_unit_type values
         being equal to 5.
       - nal_unit_type is equal to 5 for both and idr_pic_id differs in
         value.

       NOTE - Some of the VCL NAL units in redundant coded pictures or some
       non-VCL NAL units (e.g. an access unit delimiter NAL unit) may also
       be used for the detection of the boundary between access units, and
       may therefore aid in the detection of the start of a new primary
       coded picture.
    */

    bool result = false;

    if (m_prev_frame_num != -1)
    {
        ///< Need previous slice information for comparison
        if (m_nal_unit_type != SLICE_IDR && m_frame_num != m_prev_frame_num)
        {
            result = true;
        }
        else if (m_prev_pic_parameter_set_id != -1 && m_pic_parameter_set_id != m_prev_pic_parameter_set_id)
        {
            result = true;
        }
        else if (m_field_pic_flag != m_prev_field_pic_flag)
        {
            result = true;
        }
        else if ((m_bottom_field_flag != -1 && m_prev_bottom_field_flag != -1) && m_bottom_field_flag != m_prev_bottom_field_flag)
        {
            result = true;
        }
        else if ((m_nal_ref_idc == 0 || m_prev_nal_ref_idc == 0) && m_nal_ref_idc != m_prev_nal_ref_idc)
        {
            result = true;
        }
        else if ((m_pic_order_cnt_type == 0 && m_prev_pic_order_cnt_type == 0) &&
                 (m_pic_order_cnt_lsb != m_prev_pic_order_cnt_lsb || m_delta_pic_order_cnt_bottom != m_prev_delta_pic_order_cnt_bottom))
        {
            result = true;
        }
        else if ((m_pic_order_cnt_type == 1 && m_prev_pic_order_cnt_type == 1) &&
                 (m_delta_pic_order_cnt[0] != m_prev_delta_pic_order_cnt[0] || m_delta_pic_order_cnt[1] != m_prev_delta_pic_order_cnt[1]))
        {
            result = true;
        }
        else if ((m_nal_unit_type == SLICE_IDR || m_prev_nal_unit_type == SLICE_IDR) && m_nal_unit_type != m_prev_nal_unit_type)
        {
            result = true;
        }
        else if ((m_nal_unit_type == SLICE_IDR && m_prev_nal_unit_type == SLICE_IDR) && m_idr_pic_id != m_prev_idr_pic_id)
        {
            result = true;
        }
    }

    m_prev_frame_num                    = m_frame_num;
    m_prev_pic_parameter_set_id         = m_pic_parameter_set_id;
    m_prev_field_pic_flag               = m_field_pic_flag;
    m_prev_bottom_field_flag            = m_bottom_field_flag;
    m_prev_nal_ref_idc                  = m_nal_ref_idc;
    m_prev_pic_order_cnt_lsb            = m_pic_order_cnt_lsb;
    m_prev_delta_pic_order_cnt_bottom   = m_delta_pic_order_cnt_bottom;
    m_prev_delta_pic_order_cnt[0]       = m_delta_pic_order_cnt[0];
    m_prev_delta_pic_order_cnt[1]       = m_delta_pic_order_cnt[1];
    m_prev_nal_unit_type                = m_nal_unit_type;
    m_prev_idr_pic_id                   = m_idr_pic_id;

    return result;
}

void
AVCFrameChecker::resetRBSP(void)
{
    m_rbsp_index            = 0;
    m_consecutive_zeros     = 0;
    m_have_unfinished_NAL   = false;
}

bool
AVCFrameChecker::fillRBSP(const uint8_t *byteP, uint32_t byte_count, bool found_start_code)
{
    ///< bitstream buffer must be FF_INPUT_BUFFER_PADDING_SIZE bytes larger than the actual data
    uint32_t required_size = m_rbsp_index + byte_count + FF_INPUT_BUFFER_PADDING_SIZE;
    if (m_rbsp_buffer_size < required_size)
    {
        ///< Round up to packet size
        required_size = ((required_size / 188) + 1) * 188;
        ///< Need a bigger buffer
        uint8_t *new_buffer = new(std::nothrow) uint8_t[required_size];

        if (new_buffer == NULL)
        {
            ///< Allocation failed. Discard the new bytes
            log("H264Parser::fillRBSP: FAILED to allocate RBSP buffer!");
            return false;
        }

        ///< copy across bytes from old buffer
        memcpy(new_buffer, m_rbsp_buffer, m_rbsp_index);
        delete[] m_rbsp_buffer;

        m_rbsp_buffer       = new_buffer;
        m_rbsp_buffer_size  = required_size;
    }

    ///< fill rbsp while we have data
    while (byte_count)
    {
        ///< copy the byte into the rbsp, unless it is the 0x03 in a 0x000003
        if (m_consecutive_zeros < 2 || *byteP != 0x03)
        {
            m_rbsp_buffer[m_rbsp_index++] = *byteP;
        }

        if (*byteP == 0)
        {
            ++m_consecutive_zeros;

            assert(m_consecutive_zeros <= 3);
            if (m_consecutive_zeros == 3)
            {
                assert(*(byteP + 1) == 1);
            }
        }
        else
        {
            m_consecutive_zeros = 0;
        }

        ++byteP;
        --byte_count;
    }

    /* If we've found the next start code then that, plus the first byte of
     * the next NAL, plus the preceding zero bytes will all be in the rbsp
     * buffer. Move rbsp_index++ back to the end of the actual rbsp data. We
     * need to know the correct size of the rbsp to decode some NALs.
     */
    if (found_start_code)
    {
        if (m_rbsp_index >= 4)
        {
            m_rbsp_index -= 4;
            while (m_rbsp_index > 0 && m_rbsp_buffer[m_rbsp_index - 1] == 0) ///< zhp: last byte of a nalu is not '\0'
            {
                --m_rbsp_index;
            }
        }
        else
        {
            /* This should never happen. */
            assert(0);
            log("H264Parser::fillRBSP: Found start code, rbsp_index is %d but it should be >4", m_rbsp_index);
        }
    }

    ///< stick some 0xff on the end for get_bits to run into
    memset(&m_rbsp_buffer[m_rbsp_index], 0xff, FF_INPUT_BUFFER_PADDING_SIZE);
    return true;
}

// == NAL_type AU_delimiter: primary_pic_type = 5
int
AVCFrameChecker::isKeySlice(uint32_t slice_type)
{
    return (slice_type == SLICE_I   || slice_type == SLICE_SI ||
            slice_type == SLICE_I_a || slice_type == SLICE_SI_a);
}

bool
AVCFrameChecker::isSlice(uint8_t nal_type)
{
    return (nal_type == SLICE || nal_type == SLICE_DPA || nal_type == SLICE_IDR);
}

void
AVCFrameChecker::set_AU_pending()
{
    if (!m_au_pending)
    {
        m_au_pending = true;
        m_au_offset  = m_pkt_offset;
        m_au_contains_keyframe_message = false;

        finish_au(); ///< store the last AU if any.
    }
}

void
AVCFrameChecker::flush()
{
    if (m_have_unfinished_NAL)
    {
        finish_nalu();
        finish_au();
        resetRBSP();
    }
}

uint32_t
AVCFrameChecker::addBytes(const uint8_t * bytes, const uint32_t byte_count, const uint64_t stream_offset)
{
    const uint8_t *startP = bytes;
    const uint8_t *endP;
    bool           found_start_code;
    bool           good_nal_unit;

    m_on_frame     = false;
    m_on_key_frame = false;

    ///< parse at most 'byte_count' bytes until we get a new frame.
    while (startP < bytes + byte_count && !m_on_frame)
    {
        endP = find_start_code(startP, bytes + byte_count, &m_sync_accumulator);

        found_start_code = (m_sync_accumulator & 0xffffff00) == 0x00000100;

        fill_nalu(startP, endP - startP, found_start_code);

        /* Between startP and endP we potentially have some more
         * bytes of a NAL that we've been parsing (plus some bytes of the next start code)
         */
        if (m_have_unfinished_NAL)
        {
            ///< append all bytes to the RBSP buffer before the start code if there is any.
            if (!fillRBSP(startP, endP - startP, found_start_code))
            {
                resetRBSP();
                return endP - bytes;
            }

            ///< process one NALU (call may set m_have_unfinished_NAL to false)
            processRBSP(found_start_code);
        }

        ///< process the current NAL
        ///< we have dealt with every byte up to endP
        startP = endP;

        ///< a new NAL starts
        if (found_start_code)
        {
            assert(!m_have_unfinished_NAL);
            if (m_have_unfinished_NAL)
            {
                /* We've found a new start code, without completely parsing the previous NAL.
                 * Either there's a problem with the stream or with this parser.
                 */
                log("H264Parser::addBytes: Found new start code, but previous NAL is incomplete!");
            }

            /* Prepare for the new NAL */
            resetRBSP();

            /* If we find the start of an AU somewhere from here to the next start code,
             * the offset to associate with it is the one passed in to this call,
             * not any of the subsequent calls.
             */
            ///< offset of the current NAL
            m_long_start_code = (m_sync_accumulator & 0x000000ff00000000) == 0;
            m_pkt_offset      = stream_offset + (startP - (m_long_start_code ? 5 : 4) - bytes);

            /* nal_unit_type specifies the type of RBSP data structure
               contained in the NAL unit as specified in Table 7-1.
               VCL NAL units are specified as those NAL units having
               nal_unit_type equal to 1 to 5, inclusive. All remaining
               NAL units are called non-VCL NAL units:

               0  Unspecified
               1  Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
               2  Coded slice data partition A slice_data_partition_a_layer_rbsp( )
               3  Coded slice data partition B slice_data_partition_b_layer_rbsp( )
               4  Coded slice data partition C slice_data_partition_c_layer_rbsp( )
               5  Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
               6  Supplemental enhancement information (SEI) 5 sei_rbsp( )
               7  Sequence parameter set (SPS) seq_parameter_set_rbsp( )
               8  Picture parameter set pic_parameter_set_rbsp( )
               9  Access unit delimiter access_unit_delimiter_rbsp( )
               10 End of sequence end_of_seq_rbsp( )
               11 End of stream end_of_stream_rbsp( )
            */
            m_nal_unit_type = m_sync_accumulator & 0x1f;
            m_nal_ref_idc   = (m_sync_accumulator >> 5) & 0x3;

            good_nal_unit   = true;
            if (m_nal_ref_idc)
            {
                /* nal_ref_idc shall be equal to 0 for all NAL units having
                 * nal_unit_type equal to 6, 9, 10, 11, or 12.
                 */
                if (m_nal_unit_type == SEI || (m_nal_unit_type >= AU_DELIMITER && m_nal_unit_type <= FILLER_DATA))
                {
                    good_nal_unit = false;
                }
            }
            else
            {
                /* nal_ref_idc shall not be equal to 0 for NAL units with
                 * nal_unit_type equal to 5
                 */
                if (m_nal_unit_type == SLICE_IDR)
                {
                    good_nal_unit = false;
                }
            }

            assert(good_nal_unit);

            if (good_nal_unit)
            {
                finish_nalu(); ///< the last NALU if any.

                if (isSlice(m_nal_unit_type) ||
                        m_nal_unit_type == SPS || m_nal_unit_type == PPS || m_nal_unit_type == SEI)
                {
                    /* This is a NAL we need to parse.
                     * We may have the body of it in the part of the stream past to us this call,
                     * or we may get the rest in subsequent calls to addBytes. Either way, we set
                     * have_unfinished_NAL, so that we start filling the RBSP buffer.
                     */
                    m_have_unfinished_NAL = true;
                }
                else if (m_nal_unit_type == AU_DELIMITER ||
                         (m_nal_unit_type > SPS_EXT && m_nal_unit_type < AUXILIARY_SLICE))
                {
                    set_AU_pending();
                }
                else
                {
                    ///< ignore all other NALs.
                    log("H264Parser::addbytes: drop one NAL unit(type = %d).", m_nal_unit_type);
                }

                write_nalu_header();
            }
            else
            {
                ///< malformed NAL, drop this one.
                log("H264Parser::addbytes: malformed NAL units.");
            }
        }
    }

    return startP - bytes;
}

void
AVCFrameChecker::processRBSP(bool rbsp_complete)
{
    GetBitContext gb;

    init_get_bits(&gb, m_rbsp_buffer, 8 * m_rbsp_index);

    if (m_nal_unit_type == SEI)
    {
        ///< SEI cannot be parsed without knowing its size.
        if (!rbsp_complete)
        {
            return;
        }

        set_AU_pending();

        decode_SEI(&gb);
    }
    else if (m_nal_unit_type == SPS)
    {
        ///< best wait until we have the whole thing
        if (!rbsp_complete)
        {
            return;
        }

        set_AU_pending();

        if (!m_seen_sps)
        {
            m_sps_offset = m_pkt_offset;
        }

        decode_SPS(&gb);
    }
    else if (m_nal_unit_type == PPS)
    {
        ///< best wait until we have the whole thing
        if (!rbsp_complete)
        {
            return;
        }

        set_AU_pending();

        decode_PPS(&gb);
    }
    else
    {
        ///< slice
        ///< Only parse the slice headers, so return only if we have insufficient bytes.
        if (!rbsp_complete/* && m_rbsp_index < MAX_SLICE_HEADER_SIZE*/) ///<!!! we wait for the whole slice just to copy the stream.
        {
            return;
        }

        decode_Header(&gb);

        m_au->type = fieldType();

        if (is_new_au())
        {
            set_AU_pending();
        }
    }

    ///< we managed to parse a sufficient prefix of the current NAL, so go onto the next.
    m_have_unfinished_NAL = false;

    if (m_au_pending && isSlice(m_nal_unit_type))
    {
        /* once we know the slice type of a new AU, we can
         * determine if it is a keyframe or just a frame
         */
        m_au_pending = false;

        m_on_frame = true;
        m_frame_start_offset = m_au_offset;

        if (m_is_keyframe || m_au_contains_keyframe_message)
        {
            m_on_key_frame = true;
            m_keyframe_start_offset = m_au_offset;
        }
    }
}

/*
 * 7.4.3 slice header semantics
 */
bool
AVCFrameChecker::decode_Header(GetBitContext *gb)
{
    m_is_keyframe = false;

    if (m_log2_max_frame_num == 0)
    {
        ///< SPS has not been parsed yet
        return false;
    }

    /* first_mb_in_slice specifies the address of the first macroblock
       in the slice. When arbitrary slice order is not allowed as
       specified in Annex A, the value of first_mb_in_slice is
       constrained as follows.

       - If separate_colour_plane_flag is equal to 0, the value of
       first_mb_in_slice shall not be less than the value of
       first_mb_in_slice for any other slice of the current picture
       that precedes the current slice in decoding order.

       - Otherwise (separate_colour_plane_flag is equal to 1), the value of
       first_mb_in_slice shall not be less than the value of
       first_mb_in_slice for any other slice of the current picture
       that precedes the current slice in decoding order and has the
       same value of colour_plane_id.
     */
    uint8_t first_mb_in_slice = get_ue_golomb(gb);

    /* slice_type specifies the coding type of the slice according to
       Table 7-6.   e.g. P, B, I, SP, SI

       When nal_unit_type is equal to 5 (IDR picture), slice_type shall
       be equal to 2, 4, 7, or 9 (I or SI)
     */
    m_slice_type = get_ue_golomb_31(gb);

    /* s->pict_type = golomb_to_pict_type[slice_type % 5];
     */

    /* pic_parameter_set_id specifies the picture parameter set in
       use. The value of pic_parameter_set_id shall be in the range of
       0 to 255, inclusive.
     */
    m_pic_parameter_set_id = get_ue_golomb(gb);

    /* separate_colour_plane_flag equal to 1 specifies that the three
       colour components of the 4:4:4 chroma format are coded
       separately. separate_colour_plane_flag equal to 0 specifies that
       the colour components are not coded separately.  When
       separate_colour_plane_flag is not present, it shall be inferred
       to be equal to 0. When separate_colour_plane_flag is equal to 1,
       the primary coded picture consists of three separate components,
       each of which consists of coded samples of one colour plane (Y,
       Cb or Cr) that each use the monochrome coding syntax. In this
       case, each colour plane is associated with a specific
       colour_plane_id value.
     */
    if (m_separate_colour_plane_flag)
    {
        get_bits(gb, 2);  ///< colour_plane_id
    }

    /* frame_num is used as an identifier for pictures and shall be
       represented by log2_max_frame_num_minus4 + 4 bits in the
       bitstream....

       If the current picture is an IDR picture, frame_num shall be equal to 0.

       When max_num_ref_frames is equal to 0, slice_type shall be equal to 2, 4, 7, or 9.
    */
    m_frame_num = get_bits(gb, m_log2_max_frame_num);

    /* field_pic_flag equal to 1 specifies that the slice is a slice of a
       coded field. field_pic_flag equal to 0 specifies that the slice is a
       slice of a coded frame. When field_pic_flag is not present it shall be
       inferred to be equal to 0.

       bottom_field_flag equal to 1 specifies that the slice is part of a
       coded bottom field. bottom_field_flag equal to 0 specifies that the
       picture is a coded top field. When this syntax element is not present
       for the current slice, it shall be inferred to be equal to 0.
    */
    if (!m_frame_mbs_only_flag)
    {
        m_field_pic_flag = get_bits1(gb);
        m_bottom_field_flag = m_field_pic_flag ? get_bits1(gb) : 0;
    }
    else
    {
        m_field_pic_flag = 0;
        m_bottom_field_flag = -1;
    }

    /* idr_pic_id identifies an IDR picture. The values of idr_pic_id
       in all the slices of an IDR picture shall remain unchanged. When
       two consecutive access units in decoding order are both IDR
       access units, the value of idr_pic_id in the slices of the first
       such IDR access unit shall differ from the idr_pic_id in the
       second such IDR access unit. The value of idr_pic_id shall be in
       the range of 0 to 65535, inclusive.
     */
    if (m_nal_unit_type == SLICE_IDR)
    {
        m_idr_pic_id = get_ue_golomb(gb);
        m_is_keyframe = true;
    }
    else
    {
        m_is_keyframe = (m_I_is_keyframe && isKeySlice(m_slice_type));
    }

    /* pic_order_cnt_lsb specifies the picture order count modulo
       MaxPicOrderCntLsb for the top field of a coded frame or for a coded
       field. The size of the pic_order_cnt_lsb syntax element is
       log2_max_pic_order_cnt_lsb_minus4 + 4 bits. The value of the
       pic_order_cnt_lsb shall be in the range of 0 to MaxPicOrderCntLsb â€?1,
       inclusive.

       delta_pic_order_cnt_bottom specifies the picture order count
       difference between the bottom field and the top field of a coded
       frame.
    */
    if (m_pic_order_cnt_type == 0)
    {
        m_pic_order_cnt_lsb = get_bits(gb, m_log2_max_pic_order_cnt_lsb);

        if ((m_pic_order_present_flag == 1) && !m_field_pic_flag)
        {
            m_delta_pic_order_cnt_bottom = get_se_golomb(gb);
        }
        else
        {
            m_delta_pic_order_cnt_bottom = 0;
        }
    }
    else
    {
        m_delta_pic_order_cnt_bottom = 0;
    }

    /* delta_pic_order_always_zero_flag equal to 1 specifies that
       delta_pic_order_cnt[ 0 ] and delta_pic_order_cnt[ 1 ] are not
       present in the slice headers of the sequence and shall be
       inferred to be equal to 0. delta_pic_order_always_zero_flag
       equal to 0 specifies that delta_pic_order_cnt[ 0 ] is present in
       the slice headers of the sequence and delta_pic_order_cnt[ 1 ]
       may be present in the slice headers of the sequence.
    */
    if (m_delta_pic_order_always_zero_flag)
    {
        m_delta_pic_order_cnt[1] = m_delta_pic_order_cnt[0] = 0;
    }
    else if (m_pic_order_cnt_type == 1)
    {
        /* delta_pic_order_cnt[ 0 ] specifies the picture order count
           difference from the expected picture order count for the top
           field of a coded frame or for a coded field as specified in
           subclause 8.2.1. The value of delta_pic_order_cnt[ 0 ] shall
           be in the range of -2^31 to 2^31 - 1, inclusive. When this
           syntax element is not present in the bitstream for the
           current slice, it shall be inferred to be equal to 0.

           delta_pic_order_cnt[ 1 ] specifies the picture order count
           difference from the expected picture order count for the
           bottom field of a coded frame specified in subclause
           8.2.1. The value of delta_pic_order_cnt[ 1 ] shall be in the
           range of -2^31 to 2^31 - 1, inclusive. When this syntax
           element is not present in the bitstream for the current
           slice, it shall be inferred to be equal to 0.
        */
        m_delta_pic_order_cnt[0] = get_se_golomb(gb);

        if ((m_pic_order_present_flag == 1) && !m_field_pic_flag)
        {
            m_delta_pic_order_cnt[1] = get_se_golomb(gb);
        }
        else
        {
            m_delta_pic_order_cnt[1] = 0;
        }
    }

    /* redundant_pic_cnt shall be equal to 0 for slices and slice data
       partitions belonging to the primary coded picture. The
       redundant_pic_cnt shall be greater than 0 for coded slices and
       coded slice data partitions in redundant coded pictures.  When
       redundant_pic_cnt is not present, its value shall be inferred to
       be equal to 0. The value of redundant_pic_cnt shall be in the
       range of 0 to 127, inclusive.
    */
    m_redundant_pic_cnt = m_redundant_pic_cnt_present_flag ? get_ue_golomb(gb) : 0;

    return true;
}

/*
 * libavcodec used for example
 */
void
AVCFrameChecker::decode_SPS(GetBitContext * gb)
{
    int profile_idc;
    int lastScale;
    int nextScale;
    int deltaScale;

    m_seen_sps = true;

    profile_idc = get_bits(gb, 8);
    get_bits1(gb);      // constraint_set0_flag
    get_bits1(gb);      // constraint_set1_flag
    get_bits1(gb);      // constraint_set2_flag
    get_bits1(gb);      // constraint_set3_flag
    get_bits(gb, 4);    // reserved
    get_bits(gb, 8);    // level_idc
    get_ue_golomb(gb);  // sps_id

    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
            profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
            profile_idc == 86  || profile_idc == 118 || profile_idc == 128 )
    {
        // high profile
        if ((m_chroma_format_idc = get_ue_golomb(gb)) == 3)
        {
            m_separate_colour_plane_flag = (get_bits1(gb) == 1);
        }

        get_ue_golomb(gb);     // bit_depth_luma_minus8
        get_ue_golomb(gb);     // bit_depth_chroma_minus8
        get_bits1(gb);         // qpprime_y_zero_transform_bypass_flag

        if (get_bits1(gb))     // seq_scaling_matrix_present_flag
        {
            for (int idx = 0; idx < ((m_chroma_format_idc != 3) ? 8 : 12); ++idx)
            {
                if (get_bits1(gb)) // Scaling list present
                {
                    lastScale = nextScale = 8;

                    int sl_n = ((idx < 6) ? 16 : 64);

                    for(int sl_i = 0; sl_i < sl_n; ++sl_i)
                    {
                        if (nextScale != 0)
                        {
                            deltaScale = get_se_golomb(gb);
                            nextScale = (lastScale + deltaScale + 256) % 256;
                        }

                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    /*
      log2_max_frame_num_minus4 specifies the value of the variable
      MaxFrameNum that is used in frame_num related derivations as
      follows:

       MaxFrameNum = 2( log2_max_frame_num_minus4 + 4 )
     */
    m_log2_max_frame_num = get_ue_golomb(gb) + 4;

    int  offset_for_non_ref_pic;
    int  offset_for_top_to_bottom_field;
    uint32_t tmp;

    /*
      pic_order_cnt_type specifies the method to decode picture order
      count (as specified in subclause 8.2.1). The value of
      pic_order_cnt_type shall be in the range of 0 to 2, inclusive.
     */
    m_pic_order_cnt_type = get_ue_golomb(gb);
    if (m_pic_order_cnt_type == 0)
    {
        /*
          log2_max_pic_order_cnt_lsb_minus4 specifies the value of the
          variable MaxPicOrderCntLsb that is used in the decoding
          process for picture order count as specified in subclause
          8.2.1 as follows:

          MaxPicOrderCntLsb = 2( log2_max_pic_order_cnt_lsb_minus4 + 4 )

          The value of log2_max_pic_order_cnt_lsb_minus4 shall be in
          the range of 0 to 12, inclusive.
         */
        m_log2_max_pic_order_cnt_lsb = get_ue_golomb(gb) + 4;
    }
    else if (m_pic_order_cnt_type == 1)
    {
        /*
          delta_pic_order_always_zero_flag equal to 1 specifies that
          delta_pic_order_cnt[ 0 ] and delta_pic_order_cnt[ 1 ] are
          not present in the slice headers of the sequence and shall
          be inferred to be equal to
          0. delta_pic_order_always_zero_flag
         */
        m_delta_pic_order_always_zero_flag = get_bits1(gb);

        /*
          offset_for_non_ref_pic is used to calculate the picture
          order count of a non-reference picture as specified in
          8.2.1. The value of offset_for_non_ref_pic shall be in the
          range of -231 to 231 - 1, inclusive.
         */
        offset_for_non_ref_pic = get_se_golomb(gb);

        /*
          offset_for_top_to_bottom_field is used to calculate the
          picture order count of a bottom field as specified in
          subclause 8.2.1. The value of offset_for_top_to_bottom_field
          shall be in the range of -231 to 231 - 1, inclusive.
         */
        offset_for_top_to_bottom_field = get_se_golomb(gb);

        /*
          offset_for_ref_frame[ i ] is an element of a list of
          num_ref_frames_in_pic_order_cnt_cycle values used in the
          decoding process for picture order count as specified in
          subclause 8.2.1. The value of offset_for_ref_frame[ i ]
          shall be in the range of -231 to 231 - 1, inclusive.
         */
        tmp = get_ue_golomb(gb);
        for (uint32_t idx = 0; idx < tmp; ++idx)
            get_se_golomb(gb);  // offset_for_ref_frame[i]
    }

    (void) offset_for_non_ref_pic; // suppress unused var warning
    (void) offset_for_top_to_bottom_field; // suppress unused var warning

    /*
      num_ref_frames specifies the maximum number of short-term and
      long-term reference frames, complementary reference field pairs,
      and non-paired reference fields that may be used by the decoding
      process for inter prediction of any picture in the
      sequence. num_ref_frames also determines the size of the sliding
      window operation as specified in subclause 8.2.5.3. The value of
      num_ref_frames shall be in the range of 0 to MaxDpbSize (as
      specified in subclause A.3.1 or A.3.2), inclusive.
     */
    m_num_ref_frames = get_ue_golomb(gb);
    /*
      gaps_in_frame_num_value_allowed_flag specifies the allowed
      values of frame_num as specified in subclause 7.4.3 and the
      decoding process in case of an inferred gap between values of
      frame_num as specified in subclause 8.2.5.2.
     */
    /* bool gaps_in_frame_num_allowed_flag = */ get_bits1(gb);

    /*
      pic_width_in_mbs_minus1 plus 1 specifies the width of each
      decoded picture in units of macroblocks.  16 macroblocks in a row
     */
    m_pic_width = (get_ue_golomb(gb) + 1) * 16;
    /*
      pic_height_in_map_units_minus1 plus 1 specifies the height in
      slice group map units of a decoded frame or field.  16
      macroblocks in each column.
     */
    m_pic_height = (get_ue_golomb(gb) + 1) * 16;

    /*
      frame_mbs_only_flag equal to 0 specifies that coded pictures of
      the coded video sequence may either be coded fields or coded
      frames. frame_mbs_only_flag equal to 1 specifies that every
      coded picture of the coded video sequence is a coded frame
      containing only frame macroblocks.
     */
    m_frame_mbs_only_flag = get_bits1(gb);
    if (!m_frame_mbs_only_flag)
    {
        m_pic_height *= 2;

        /*
          mb_adaptive_frame_field_flag equal to 0 specifies no
          switching between frame and field macroblocks within a
          picture. mb_adaptive_frame_field_flag equal to 1 specifies
          the possible use of switching between frame and field
          macroblocks within frames. When mb_adaptive_frame_field_flag
          is not present, it shall be inferred to be equal to 0.
         */
        get_bits1(gb); // mb_adaptive_frame_field_flag
    }

    get_bits1(gb);     // direct_8x8_inference_flag

    /*
      frame_cropping_flag equal to 1 specifies that the frame cropping
      offset parameters follow next in the sequence parameter
      set. frame_cropping_flag equal to 0 specifies that the frame
      cropping offset parameters are not present.
     */
    if (get_bits1(gb)) // frame_cropping_flag
    {
        m_frame_crop_left_offset = get_ue_golomb(gb);
        m_frame_crop_right_offset = get_ue_golomb(gb);
        m_frame_crop_top_offset = get_ue_golomb(gb);
        m_frame_crop_bottom_offset = get_ue_golomb(gb);
    }

    /*
      vui_parameters_present_flag equal to 1 specifies that the
      vui_parameters( ) syntax structure as specified in Annex E is
      present. vui_parameters_present_flag equal to 0 specifies that
      the vui_parameters( ) syntax structure as specified in Annex E
      is not present.
     */
    if (get_bits1(gb)) // vui_parameters_present_flag
    {
        vui_parameters(gb);
    }
}

void
AVCFrameChecker::parse_SPS(uint8_t *sps, uint32_t sps_size, bool& interlaced, int32_t& max_ref_frames)
{
    GetBitContext gb;

    init_get_bits(&gb, sps, sps_size << 3);

    decode_SPS(&gb);

    interlaced = !m_frame_mbs_only_flag;
    max_ref_frames = m_num_ref_frames;
}

void
AVCFrameChecker::decode_PPS(GetBitContext * gb)
{
    /*
      pic_parameter_set_id identifies the picture parameter set that
      is referred to in the slice header. The value of
      pic_parameter_set_id shall be in the range of 0 to 255,
      inclusive.
     */
    m_pic_parameter_set_id = get_ue_golomb(gb);

    /*
      seq_parameter_set_id refers to the active sequence parameter
      set. The value of seq_parameter_set_id shall be in the range of
      0 to 31, inclusive.
     */
    m_seq_parameter_set_id = get_ue_golomb(gb);
    get_bits1(gb); // entropy_coding_mode_flag;

    /*
      pic_order_present_flag equal to 1 specifies that the picture
      order count related syntax elements are present in the slice
      headers as specified in subclause 7.3.3. pic_order_present_flag
      equal to 0 specifies that the picture order count related syntax
      elements are not present in the slice headers.
     */
    m_pic_order_present_flag = get_bits1(gb);

#if 0 // Rest not currently needed, and requires <math.h>
    uint num_slice_groups = get_ue_golomb(gb) + 1;
    if (num_slice_groups > 1) // num_slice_groups (minus 1)
    {
        uint idx;

        switch (get_ue_golomb(gb)) // slice_group_map_type
        {
        case 0:
            for (idx = 0; idx < num_slice_groups; ++idx)
                get_ue_golomb(gb); // run_length_minus1[idx]
            break;
        case 1:
            for (idx = 0; idx < num_slice_groups; ++idx)
            {
                get_ue_golomb(gb); // top_left[idx]
                get_ue_golomb(gb); // bottom_right[idx]
            }
            break;
        case 3:
        case 4:
        case 5:
            get_bits1(gb);     // slice_group_change_direction_flag
            get_ue_golomb(gb); // slice_group_change_rate_minus1
            break;
        case 6:
            uint pic_size_in_map_units = get_ue_golomb(gb) + 1;
            uint num_bits = (int)ceil(log2(num_slice_groups));
            for (idx = 0; idx < pic_size_in_map_units; ++idx)
            {
                get_bits(gb, num_bits); //slice_group_id[idx]
            }
        }
    }

    get_ue_golomb(gb); // num_ref_idx_10_active_minus1
    get_ue_golomb(gb); // num_ref_idx_11_active_minus1
    get_bits1(gb);     // weighted_pred_flag;
    get_bits(gb, 2);   // weighted_bipred_idc
    get_se_golomb(gb); // pic_init_qp_minus26
    get_se_golomb(gb); // pic_init_qs_minus26
    get_se_golomb(gb); // chroma_qp_index_offset
    get_bits1(gb);     // deblocking_filter_control_present_flag
    get_bits1(gb);     // constrained_intra_pref_flag
    redundant_pic_cnt_present_flag = get_bits1(gb);
#endif
}

void
AVCFrameChecker::decode_SEI(GetBitContext *gb)
{
    int   recovery_frame_cnt = -1;
    bool  exact_match_flag = false;
    bool  broken_link_flag = false;
    int   changing_group_slice_idc = -1;

    int type = 0, size = 0;

    /* A message requires at least 2 bytes, and then
     * there's the stop bit plus alignment, so there
     * can be no message in less than 24 bits */
    while (get_bits_left(gb) >= 24)
    {
        do
        {
            type += show_bits(gb, 8);
        }
        while (get_bits(gb, 8) == 0xFF);

        do
        {
            size += show_bits(gb, 8);
        }
        while (get_bits(gb, 8) == 0xFF);

        switch (type)
        {
        case SEI_TYPE_RECOVERY_POINT:
            recovery_frame_cnt = get_ue_golomb(gb);
            exact_match_flag = get_bits1(gb);
            broken_link_flag = get_bits1(gb);
            changing_group_slice_idc = get_bits(gb, 2);
            m_au_contains_keyframe_message = (recovery_frame_cnt == 0);

            if ((size - 12) > 0)
            {
                skip_bits(gb, (size - 12) * 8);
            }

            return;

        default:
            skip_bits(gb, size * 8);
            break;
        }
    }

    (void) exact_match_flag; // suppress unused var warning
    (void) broken_link_flag; // suppress unused var warning
    (void) changing_group_slice_idc; // suppress unused var warning
}

void
AVCFrameChecker::vui_parameters(GetBitContext * gb)
{
    /*
      aspect_ratio_info_present_flag equal to 1 specifies that
      aspect_ratio_idc is present. aspect_ratio_info_present_flag
      equal to 0 specifies that aspect_ratio_idc is not present.
     */
    if (get_bits1(gb)) //aspect_ratio_info_present_flag
    {
        /*
          aspect_ratio_idc specifies the value of the sample aspect
          ratio of the luma samples. Table E-1 shows the meaning of
          the code. When aspect_ratio_idc indicates Extended_SAR, the
          sample aspect ratio is represented by sar_width and
          sar_height. When the aspect_ratio_idc syntax element is not
          present, aspect_ratio_idc value shall be inferred to be
          equal to 0.
         */
        m_aspect_ratio_idc = get_bits(gb, 8);

        switch (m_aspect_ratio_idc)
        {
        case 0:
            // Unspecified
            break;
        case 1:
            // 1:1
            /*
              1280x720 16:9 frame without overscan
              1920x1080 16:9 frame without overscan (cropped from 1920x1088)
              640x480 4:3 frame without overscan
             */
            break;
        case 2:
            // 12:11
            /*
              720x576 4:3 frame with horizontal overscan
              352x288 4:3 frame without overscan
             */
            break;
        case 3:
            // 10:11
            /*
              720x480 4:3 frame with horizontal overscan
              352x240 4:3 frame without overscan
             */
            break;
        case 4:
            // 16:11
            /*
              720x576 16:9 frame with horizontal overscan
              540x576 4:3 frame with horizontal overscan
             */
            break;
        case 5:
            // 40:33
            /*
              720x480 16:9 frame with horizontal overscan
              540x480 4:3 frame with horizontal overscan
             */
            break;
        case 6:
            // 24:11
            /*
              352x576 4:3 frame without overscan
              540x576 16:9 frame with horizontal overscan
             */
            break;
        case 7:
            // 20:11
            /*
              352x480 4:3 frame without overscan
              480x480 16:9 frame with horizontal overscan
             */
            break;
        case 8:
            // 32:11
            /*
              352x576 16:9 frame without overscan
             */
            break;
        case 9:
            // 80:33
            /*
              352x480 16:9 frame without overscan
             */
            break;
        case 10:
            // 18:11
            /*
              480x576 4:3 frame with horizontal overscan
             */
            break;
        case 11:
            // 15:11
            /*
              480x480 4:3 frame with horizontal overscan
             */
            break;
        case 12:
            // 64:33
            /*
              540x576 16:9 frame with horizontal overscan
             */
            break;
        case 13:
            // 160:99
            /*
              540x576 16:9 frame with horizontal overscan
             */
            break;
        case EXTENDED_SAR:
            m_sar_width = get_bits(gb, 16);
            m_sar_height = get_bits(gb, 16);
            break;
        }
    }
    else
    {
        m_sar_width = m_sar_height = 0;
    }

    if (get_bits1(gb)) //overscan_info_present_flag
    {
        get_bits1(gb); //overscan_appropriate_flag
    }

    if (get_bits1(gb)) //video_signal_type_present_flag
    {
        get_bits(gb, 3); //video_format
        get_bits1(gb);   //video_full_range_flag

        if (get_bits1(gb)) // colour_description_present_flag
        {
            get_bits(gb, 8); // colour_primaries
            get_bits(gb, 8); // transfer_characteristics
            get_bits(gb, 8); // matrix_coefficients
        }
    }

    if (get_bits1(gb)) //chroma_loc_info_present_flag
    {
        get_ue_golomb(gb); //chroma_sample_loc_type_top_field ue(v)
        get_ue_golomb(gb); //chroma_sample_loc_type_bottom_field ue(v)
    }

    if (get_bits1(gb)) //timing_info_present_flag
    {
        m_unitsInTick = get_bits_long(gb, 32); //num_units_in_tick
        m_timeScale   = get_bits_long(gb, 32);   //time_scale
        m_fixedRate   = get_bits1(gb);
    }
}

H264AU *
AVCFrameChecker::getAU()
{
    H264AU * au = NULL;

    pthread_mutex_lock(&m_mutex);

    if (!m_aus.empty())
    {
        au = m_aus.front();
        m_aus.pop_front();
    }

    pthread_mutex_unlock(&m_mutex);

    return au;
}

bool
AVCFrameChecker::frameStart() const
{
    return m_on_frame;
}

bool
AVCFrameChecker::keyFrameStart() const
{
    return m_on_key_frame;
}

uint32_t
AVCFrameChecker::pictureWidth() const
{
    return m_pic_width;
}

uint32_t
AVCFrameChecker::pictureHeight() const
{
    return m_pic_height;
}

uint64_t
AVCFrameChecker::frameAUstreamOffset() const
{
    return m_frame_start_offset;
}

uint64_t
AVCFrameChecker::keyframeAUstreamOffset() const
{
    return m_keyframe_start_offset;
}

uint64_t
AVCFrameChecker::SPSstreamOffset() const
{
    return m_sps_offset;
}

double
AVCFrameChecker::frameRate() const
{
    uint64_t num;
    double   fps;

    num = 500 * (uint64_t)m_timeScale; /* 1000 * 0.5 */
    fps = (m_unitsInTick != 0 ? num / (double)m_unitsInTick : 0) / 1000;

    return fps;

    //if (m_unitsInTick == 0)
    //{
    //    return 0;
    //}
    //else if (m_timeScale & 0x1)
    //{
    //    return m_timeScale / m_unitsInTick * 2.;
    //}
    //else
    //{
    //    return m_timeScale / 2. / m_unitsInTick;
    //}
}

///< computes aspect ratio from picture size and sample aspect ratio
uint32_t
AVCFrameChecker::aspectRatio() const
{
    double aspect = 0.0;

    if (m_pic_height)
    {
        aspect = pictureWidthCropped() / (double)pictureHeightCropped();
    }

    switch (m_aspect_ratio_idc)
    {
    case 0:
        // Unspecified
        break;
    case 1:
        // 1:1
        break;
    case 2:
        // 12:11
        aspect *= 1.0909090909090908;
        break;
    case 3:
        // 10:11
        aspect *= 0.90909090909090906;
        break;
    case 4:
        // 16:11
        aspect *= 1.4545454545454546;
        break;
    case 5:
        // 40:33
        aspect *= 1.2121212121212122;
        break;
    case 6:
        // 24:11
        aspect *= 2.1818181818181817;
        break;
    case 7:
        // 20:11
        aspect *= 1.8181818181818181;
        break;
    case 8:
        // 32:11
        aspect *= 2.9090909090909092;
        break;
    case 9:
        // 80:33
        aspect *= 2.4242424242424243;
        break;
    case 10:
        // 18:11
        aspect *= 1.6363636363636365;
        break;
    case 11:
        // 15:11
        aspect *= 1.3636363636363635;
        break;
    case 12:
        // 64:33
        aspect *= 1.9393939393939394;
        break;
    case 13:
        // 160:99
        aspect *= 1.6161616161616161;
        break;
    case 14:
        // 4:3
        aspect *= 1.3333333333333333;
        break;
    case 15:
        // 3:2
        aspect *= 1.5;
        break;
    case 16:
        // 2:1
        aspect *= 2.0;
        break;
    case EXTENDED_SAR:
        if (m_sar_height)
            aspect *= m_sar_width / (double)m_sar_height;
        else
            aspect = 0.0;
        break;
    }

    if (aspect == 0.0)
    {
        return 0;
    }

    if (fabs(aspect - 1.3333333333333333) < eps)
    {
        return 2;
    }

    if (fabs(aspect - 1.7777777777777777) < eps)
    {
        return 3;
    }

    if (fabs(aspect - 2.21) < eps)
    {
        return 4;
    }

    return aspect * 1000000;
}

// Following the lead of libavcodec, ignore the left cropping.
uint32_t
AVCFrameChecker::pictureWidthCropped() const
{
    uint32_t ChromaArrayType = m_separate_colour_plane_flag ? 0 : m_chroma_format_idc;
    uint32_t CropUnitX = 1;
    uint32_t SubWidthC = m_chroma_format_idc == 3 ? 1 : 2;

    if (ChromaArrayType != 0)
    {
        CropUnitX = SubWidthC;
    }

    uint32_t crop = CropUnitX * m_frame_crop_right_offset;
    return m_pic_width - crop;
}

// Following the lead of libavcodec, ignore the top cropping.
uint32_t
AVCFrameChecker::pictureHeightCropped() const
{
    uint32_t ChromaArrayType = m_separate_colour_plane_flag ? 0 : m_chroma_format_idc;
    uint32_t CropUnitY = 2 - m_frame_mbs_only_flag;
    uint32_t SubHeightC = m_chroma_format_idc <= 1 ? 2 : 1;

    if (ChromaArrayType != 0)
    {
        CropUnitY *= SubHeightC;
    }

    uint32_t crop = CropUnitY * m_frame_crop_bottom_offset;
    return m_pic_height - crop;
}

frame_type
AVCFrameChecker::fieldType() const
{
    if (m_bottom_field_flag == -1)
    {
        return FRAME;
    }
    else
    {
        return m_bottom_field_flag ? FIELD_BOTTOM : FIELD_TOP;
    }
}

const uint8_t *
AVCFrameChecker::find_start_code(const uint8_t * p, const uint8_t *end, uint64_t * state)
{
    while (p < end)
    {
        uint64_t tmp = *state << 8;
        *state = tmp + *(p++);

        if ((tmp & 0xffffffff) == 0x100)
        {
            return p;
        }
    }

    return end;
}

#if 0
const uint8_t *
AVCFrameChecker::find_start_code(const uint8_t * p, const uint8_t *end, uint32_t * state)
{
    ///< FFMPEG's way of checking a start code, returning the first byte of a new NAL or end.
    int i;

    if (p >= end)
    {
        return end;
    }

    for (i = 0; i < 3; i++)
    {
        uint32_t tmp = *state << 8;
        *state = tmp + *(p++);

        if (tmp == 0x100 || p == end)
        {
            return p;
        }
    }

    while (p < end)
    {
        if (p[-1] > 1)
        {
            p += 3;
        }
        else if (p[-2])
        {
            p += 2;
        }
        else if (p[-3] | (p[-1] - 1)) ///< bitwise 'or'
        {
            p++;
        }
        else
        {
            p++;
            break;
        }
    }

    p = FFMIN(p, end) - 4;

    *state = AV_RB32(p);

    return p + 4;
}
#endif

void
AVCFrameChecker::log(const char * format, ...)
{
    char message[1024] = { 0 };

    va_list arg_ptr;
    va_start(arg_ptr, format);

    vsnprintf(message, 1024, format, arg_ptr);

    va_end(arg_ptr);

    printf(message);    ///< write console
}

//////////////////////////////////////////////////////////////////////////
////
