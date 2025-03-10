/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2022 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_QAT_HEADERS
#include <qat/cpa.h>
#include <qat/cpa_dc.h>
#else
#include <cpa.h>
#include <cpa_dc.h>
#endif
#include "qatzip.h"
#include "qatzip_internal.h"
#include "qz_utils.h"

#pragma pack(push, 1)

inline unsigned long qzGzipHeaderSz(void)
{
    return sizeof(QzGzH_T);
}

inline unsigned long stdGzipHeaderSz(void)
{
    return sizeof(StdGzH_T);
}

inline unsigned long qz4BHeaderSz(void)
{
    return sizeof(Qz4BH_T);
}

inline unsigned long stdGzipFooterSz(void)
{
    return sizeof(StdGzF_T);
}

// LZ4S header should have the same size with LZ4.
// Note: QAT HW compressed LZ4S block without block size at block beginning.
//      Combind LZ4 frame header size and SW block size as LZ4S header size.
inline unsigned long qzLZ4SHeaderSz(void)
{
    //Lz4 frame header size + block header size
    return qzLZ4HeaderSz() + QZ_LZ4_BLK_HEADER_SIZE;
}

inline unsigned long outputFooterSz(DataFormatInternal_T data_fmt)
{
    unsigned long size = 0;
    switch (data_fmt) {
    case DEFLATE_4B:
    /* fall through */
    case DEFLATE_RAW:
        size = 0;
        break;
    case LZ4_FH:
    case LZ4S_FH:
    case ZSTD_RAW:   //same as lz4 footer
        size = qzLZ4FooterSz();
        break;
    case DEFLATE_GZIP_EXT:
    default:
        size = stdGzipFooterSz();
        break;
    }

    return size;
}

unsigned long outputHeaderSz(DataFormatInternal_T data_fmt)
{
    unsigned long size = 0;

    switch (data_fmt) {
    case DEFLATE_4B:
        size = qz4BHeaderSz();
        break;
    case DEFLATE_RAW:
        break;
    case DEFLATE_GZIP:
        size = stdGzipHeaderSz();
        break;
    case LZ4_FH:
        size = qzLZ4HeaderSz();
        break;
    case LZ4S_FH:
    case ZSTD_RAW:
        size = qzLZ4SHeaderSz();
        break;
    case DEFLATE_GZIP_EXT:
    default:
        size = qzGzipHeaderSz();
        break;
    }

    return size;
}

void qzGzipHeaderExtraFieldGen(unsigned char *ptr, CpaDcRqResults *res)
{
    QzExtraField_T *extra;
    extra = (QzExtraField_T *)ptr;

    extra->st1        = 'Q';
    extra->st2        = 'Z';
    extra->x2_len      = (uint16_t)sizeof(extra->qz_e);
    extra->qz_e.src_sz  = res->consumed;
    extra->qz_e.dest_sz = res->produced;
}

void qzGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    QzGzH_T *hdr;

    hdr = (QzGzH_T *)ptr;
    hdr->std_hdr.id1      = 0x1f;
    hdr->std_hdr.id2      = 0x8b;
    hdr->std_hdr.cm       = QZ_DEFLATE;
    hdr->std_hdr.flag     = 0x04; /*Fextra BIT SET*/
    hdr->std_hdr.mtime[0] = (char)0;
    hdr->std_hdr.mtime[1] = (char)0;
    hdr->std_hdr.mtime[2] = (char)0;
    hdr->std_hdr.mtime[3] = (char)0;
    hdr->std_hdr.xfl      = 0;
    hdr->std_hdr.os       = 255;
    hdr->x_len     = (uint16_t)sizeof(hdr->extra);
    qzGzipHeaderExtraFieldGen((unsigned char *)&hdr->extra, res);
}

void stdGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    StdGzH_T *hdr;

    hdr = (StdGzH_T *)ptr;
    hdr->id1      = 0x1f;
    hdr->id2      = 0x8b;
    hdr->cm       = QZ_DEFLATE;
    hdr->flag     = 0x00;
    hdr->mtime[0] = (char)0;
    hdr->mtime[1] = (char)0;
    hdr->mtime[2] = (char)0;
    hdr->mtime[3] = (char)0;
    hdr->xfl      = 0;
    hdr->os       = 255;
}

void qz4BHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    Qz4BH_T *hdr;
    hdr = (Qz4BH_T *)ptr;
    hdr->blk_size = res->produced;
}

/* Because QAT HW generate LZ4S block without block size at block beginning.
*  if just add LZ4 frame header and frame footer to those block, it's going
*  to be very hard to find out where is block ending. so Append block size to
*  every LZ4S block. it will make LZ4S frame have the same format just like
*  LZ4 frame.
*/
void qzLZ4SHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    //frame header contains content size
    qzLZ4HeaderGen(ptr, res);

    //block header contains block size
    unsigned int *blk_size = (unsigned int *)(ptr + sizeof(QzLZ4H_T));
    *blk_size = (unsigned int)res->produced;
}

void outputHeaderGen(unsigned char *ptr,
                     CpaDcRqResults *res,
                     DataFormatInternal_T data_fmt)
{
    QZ_DEBUG("Generate header\n");

    switch (data_fmt) {
    case DEFLATE_4B:
        qz4BHeaderGen(ptr, res);
        break;
    case DEFLATE_RAW:
        break;
    case DEFLATE_GZIP:
        stdGzipHeaderGen(ptr, res);
        break;
    case LZ4_FH:
        qzLZ4HeaderGen(ptr, res);
        break;
    case LZ4S_FH:
    case ZSTD_RAW:
        qzLZ4SHeaderGen(ptr, res);
        break;
    case DEFLATE_GZIP_EXT:
    default:
        qzGzipHeaderGen(ptr, res);
        break;
    }
}

static int isQATDeflateProcessable(const unsigned char *ptr,
                                   const unsigned int *const src_len,
                                   QzSess_T *const qz_sess)
{
    QzGzH_T *h = (QzGzH_T *)ptr;
    Qz4BH_T *h_4B;
    StdGzF_T *qzFooter = NULL;
    long buff_sz = (DEST_SZ(qz_sess->sess_params.hw_buff_sz) < *src_len ? DEST_SZ(
                        qz_sess->sess_params.hw_buff_sz) : *src_len);

    if (qz_sess->sess_params.data_fmt == DEFLATE_4B) {
        h_4B = (Qz4BH_T *)ptr;
        if (h_4B->blk_size > DEST_SZ(qz_sess->sess_params.hw_buff_sz)) {
            return 0;
        }
        return 1;
    }

    /*check if HW can process*/
    if (h->std_hdr.id1 == 0x1f       && \
        h->std_hdr.id2 == 0x8b       && \
        h->std_hdr.cm  == QZ_DEFLATE && \
        h->std_hdr.flag == 0x00) {
        qzFooter = (StdGzF_T *)(findStdGzipFooter((const unsigned char *)h, buff_sz));
        if ((unsigned char *)qzFooter - ptr - stdGzipHeaderSz() > DEST_SZ(
                qz_sess->sess_params.hw_buff_sz) ||
            qzFooter->i_size > qz_sess->sess_params.hw_buff_sz) {
            return 0;
        }
        qz_sess->sess_params.data_fmt  = DEFLATE_GZIP;
        return 1;
    }

    /* Besides standard GZIP header, only Gzip header with QZ extension can be processed by QAT */
    if (h->std_hdr.id1 != 0x1f       || \
        h->std_hdr.id2 != 0x8b       || \
        h->std_hdr.cm  != QZ_DEFLATE) {
        /* There are two possibilities when h is not a gzip header: */
        /* 1, wrong data */
        /* 2, It is the 2nd, 3rd... part of the file with the standard gzip header. */
        return -1;
    }

    return (h->extra.st1 == 'Q'  && \
            h->extra.st2 == 'Z');
}

int isQATProcessable(const unsigned char *ptr,
                     const unsigned int *const src_len,
                     QzSess_T *const qz_sess)
{
    uint32_t rc = 0;
    DataFormatInternal_T data_fmt;
    assert(ptr != NULL);
    assert(src_len != NULL);
    assert(qz_sess != NULL);


    data_fmt = qz_sess->sess_params.data_fmt;
    switch (data_fmt) {
    case DEFLATE_4B:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
        rc = isQATDeflateProcessable(ptr, src_len, qz_sess);
        break;
    case LZ4_FH:
        rc = isQATLZ4Processable(ptr, src_len, qz_sess);
        break;
    default:
        rc = 0;
        break;
    }
    return rc;
}

int qzGzipHeaderExt(const unsigned char *const ptr, QzGzH_T *hdr)
{
    QzGzH_T *h;

    h = (QzGzH_T *)ptr;
    if (h->std_hdr.id1          != 0x1f             || \
        h->std_hdr.id2          != 0x8b             || \
        h->extra.st1            != 'Q'              || \
        h->extra.st2            != 'Z'              || \
        h->std_hdr.cm           != QZ_DEFLATE       || \
        h->std_hdr.flag         != 0x04             || \
        (h->std_hdr.xfl != 0 && h->std_hdr.xfl != 2 && \
         h->std_hdr.xfl         != 4)               || \
        h->std_hdr.os           != 255              || \
        h->x_len                != sizeof(h->extra) || \
        h->extra.x2_len         != sizeof(h->extra.qz_e)) {
        QZ_DEBUG("id1: %x, id2: %x, st1: %c, st2: %c, cm: %d, flag: %d,"
                 "xfl: %d, os: %d, x_len: %d, x2_len: %d\n",
                 h->std_hdr.id1, h->std_hdr.id2, h->extra.st1, h->extra.st2,
                 h->std_hdr.cm, h->std_hdr.flag, h->std_hdr.xfl, h->std_hdr.os,
                 h->x_len, h->extra.x2_len);
        return QZ_FAIL;
    }

    QZ_MEMCPY(hdr, ptr, sizeof(*hdr), sizeof(*hdr));
    return QZ_OK;
}

void qzGzipFooterGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(NULL != ptr);
    assert(NULL != res);
    StdGzF_T *ftr;

    ftr = (StdGzF_T *)ptr;
    ftr->crc32 = res->checksum;
    ftr->i_size = res->consumed;
}

inline void outputFooterGen(QzSess_T *qz_sess,
                            CpaDcRqResults *res,
                            DataFormatInternal_T data_fmt)
{
    QZ_DEBUG("Generate footer\n");

    unsigned char *ptr = qz_sess->next_dest;
    switch (data_fmt) {
    case DEFLATE_RAW:
        break;
    case LZ4_FH:
    case LZ4S_FH:
    case ZSTD_RAW:
        qzLZ4FooterGen(ptr, res);
        break;
    case DEFLATE_GZIP_EXT:
    default:
        qzGzipFooterGen(ptr, res);
        break;
    }
}

void qzGzipFooterExt(const unsigned char *const ptr, StdGzF_T *ftr)
{
    QZ_MEMCPY(ftr, ptr, sizeof(*ftr), sizeof(*ftr));
}

unsigned char *findStdGzipFooter(const unsigned char *src_ptr,
                                 long src_avail_len)
{
    StdGzH_T *gzHeader = NULL;
    unsigned int offset = stdGzipHeaderSz() + stdGzipFooterSz();

    while (src_avail_len >= offset + stdGzipHeaderSz()) {
        gzHeader = (StdGzH_T *)(src_ptr + offset);
        if (gzHeader->id1 == 0x1f       && \
            gzHeader->id2 == 0x8b       && \
            gzHeader->cm  == QZ_DEFLATE && \
            gzHeader->flag == 0x00) {
            return (void *)((unsigned char *)gzHeader - stdGzipFooterSz());
        }
        offset++;
    }
    return (void *)((unsigned char *)src_ptr + src_avail_len - stdGzipFooterSz());
}

#pragma pack(pop)
