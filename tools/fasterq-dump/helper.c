/*===========================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ===========================================================================
*
*/

#include "helper.h"

#include <klib/log.h>
#include <klib/printf.h>
#include <klib/out.h>
#include <kfs/defs.h>
#include <kfs/file.h>
#include <kfs/buffile.h>
#include <search/nucstrstr.h>

#include <kdb/manager.h>
#include <vdb/manager.h>
#include <vfs/manager.h>
#include <vfs/path.h>

#include <atomic32.h>

#include <limits.h> /* PATH_MAX */
#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif

bool is_format_split( format_t fmt ) {
    bool res = false;
    /* to produce the same output as fastq-dump, we do not include the read-id anywhere! */
    /*
    switch( fmt ) {
        case ft_fastq_split_spot : res = true; break;
        case ft_fastq_split_file : res = true; break;
        case ft_fastq_split_3    : res = true; break;
        case ft_fasta_split_spot : res = true; break;
        case ft_fasta_split_file : res = true; break;
        case ft_fasta_split_3    : res = true; break;
        case ft_fasta_us_split_spot : res = true; break;
        default : res = false; break;
    }
    */
    return res;
}

bool is_format_fasta( format_t fmt ){
    bool res;
    switch( fmt ) {
        case ft_fasta_whole_spot : res = true; break;
        case ft_fasta_split_spot : res = true; break;
        case ft_fasta_split_file : res = true; break;
        case ft_fasta_split_3    : res = true; break;
        case ft_fasta_us_split_spot : res = true; break;
        default : res = false; break;
    }
    return res;
}

rc_t ErrMsg( const char * fmt, ... ) {
    rc_t rc;
    char buffer[ 4096 ];
    size_t num_writ;

    va_list list;
    va_start( list, fmt );
    rc = string_vprintf( buffer, sizeof buffer, &num_writ, fmt, list );
    if ( 0 == rc ) {
        rc = pLogMsg( klogErr, "$(E)", "E=%s", buffer );
    }
    va_end( list );
    return rc;
} 

rc_t CC ArgsOptionCount( const struct Args * self, const char * option_name, uint32_t * count );
rc_t CC ArgsOptionValue( const struct Args * self, const char * option_name, uint32_t iteration, const void ** value );

const char * get_str_option( const struct Args *args, const char *name, const char * dflt ) {
    const char* res = dflt;
    uint32_t count;
    rc_t rc = ArgsOptionCount( args, name, &count );
    if ( 0 == rc && count > 0 ) {
        rc = ArgsOptionValue( args, name, 0, (const void**)&res );
        if ( 0 != rc ) { res = dflt; }
    }
    return res;
}

bool get_bool_option( const struct Args *args, const char *name ) {
    uint32_t count;
    rc_t rc = ArgsOptionCount( args, name, &count );
    return ( 0 == rc && count > 0 );
}

uint64_t get_uint64_t_option( const struct Args * args, const char *name, uint64_t dflt ) {
    uint64_t res = dflt;
    const char * s = get_str_option( args, name, NULL );
    if ( NULL != s ) {
        size_t l = string_size( s );
        if ( l > 0 ) {
            char * endptr;
            res = strtol( s, &endptr, 0 );
        }
    }
    return res;
}

uint32_t get_uint32_t_option( const struct Args * args, const char *name, uint32_t dflt ) {
    uint32_t res = dflt;
    const char * s = get_str_option( args, name, NULL );
    if ( NULL != s ) {
        size_t l = string_size( s );
        if ( l > 0 ) {
            char * endptr;
            res = ( uint32_t )strtol( s, &endptr, 0 );
        }
    }
    return res;
}

size_t get_size_t_option( const struct Args * args, const char *name, size_t dflt ) {
    size_t res = dflt;
    const char * s = get_str_option( args, name, NULL );
    if ( NULL != s ) {
        size_t l = string_size( s );
        if ( l > 0 ) {
            size_t multipl = 1;
            switch( s[ l - 1 ] ) {
                case 'k' :
                case 'K' : multipl = 1024; break;
                case 'm' :
                case 'M' : multipl = 1024 * 1024; break;
                case 'g' :
                case 'G' : multipl = 1024 * 1024 * 1024; break;
            }
            if ( multipl > 1 ) {
                char * src = string_dup( s, l - 1 );
                if ( NULL != src ) {
                    char * endptr;
                    res = strtol( src, &endptr, 0 ) * multipl;
                    free( src );
                }
            } else {
                char * endptr;
                res = strtol( s, &endptr, 0 );
            }
        }
    }
    return res;
}

static format_t format_cmp( String * Format, const char * test, format_t test_fmt ) {
    String TestFormat;
    StringInitCString( &TestFormat, test );
    if ( 0 == StringCaseCompare ( Format, &TestFormat ) )  {
        return test_fmt;
    }
    return ft_unknown;
}

format_t get_format_t( const char * format,
            bool split_spot, bool split_file, bool split_3, bool whole_spot,
            bool fasta, bool fasta_us ) {
    format_t res = ft_unknown;
    if ( NULL != format && 0 != format[ 0 ] ) {
        /* the format option has been used, try to recognize one of the options,
           the legacy options will be ignored now */

        String Format;
        StringInitCString( &Format, format );

        res = format_cmp( &Format, "fastq-whole-spot", ft_fastq_whole_spot );
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fastq-split-spot", ft_fastq_split_spot );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fastq-split-file", ft_fastq_split_file );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fastq-split-3", ft_fastq_split_3 );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fasta-whole-spot", ft_fasta_whole_spot );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fasta-split-spot", ft_fasta_split_spot );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fasta-split-file", ft_fasta_split_file );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fasta-split-3", ft_fasta_split_3 );
        }
        if ( ft_unknown == res ) {
            res = format_cmp( &Format, "fasta-us-split-spot", ft_fasta_us_split_spot );
        }
    } else {
        /* the format option has not been used, let us see if some of the legacy-options
            have been used */
        if ( split_3 ) {
            res = ( fasta || fasta_us ) ? ft_fasta_split_3 : ft_fastq_split_3;
            /* split-file, split-spot and whole-spot are ignored! */
        } else if ( split_file ) {
            res = ( fasta || fasta_us ) ? ft_fasta_split_file : ft_fastq_split_file;
            /* split-3, split-spot and whole-spot are ignored! */
        } else if ( split_spot ) {
            /* split-3, split-file and whole-spot are ignored! */
            if ( fasta_us ) {
                res = ft_fasta_us_split_spot;
            } else if ( fasta ) {
                res = ft_fasta_split_spot;
            } else {
                res = ft_fastq_split_spot;
            }
        } else if ( whole_spot ) {
            /* split-3, split-file and split-spot are ignored! */
            res = ( fasta || fasta_us ) ? ft_fasta_whole_spot : ft_fastq_whole_spot;
        }
    }
    /* default to split_3 if no format has been given at all */
    if ( ft_unknown == res ) {
        if ( fasta_us ) {
            res = ft_fasta_us_split_spot;
        } else if ( fasta ) {
            res = ft_fasta_split_spot;
        } else {
            res = ft_fastq_split_3;
        }
    }
    return res;
}

rc_t make_SBuffer( SBuffer_t * buffer, size_t len ) {
    rc_t rc = 0;
    String * S = &buffer -> S;
    S -> size = S -> len = 0;
    S -> addr = malloc( len );
    if ( NULL == S -> addr ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
        ErrMsg( "make_SBuffer().malloc( %d ) -> %R", ( len ), rc );
    } else {
        buffer -> buffer_size = len;
    }
    return rc;
}

void release_SBuffer( SBuffer_t * self ) {
    if ( NULL != self ) {
        String * S = &self -> S;
        if ( NULL != S -> addr ) {
            free( ( void * ) S -> addr );
        }
    }
}

rc_t increase_SBuffer( SBuffer_t * self, size_t by ) {
    rc_t rc;
    if ( NULL == self ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcSelf, rcNull );
    } else {
        size_t new_size = self -> buffer_size + by;
        release_SBuffer( self );
        rc = make_SBuffer( self, new_size );
    }
    return rc;
}

rc_t increase_SBuffer_to( SBuffer_t * self, size_t new_size ) {
    rc_t rc = 0;
    if ( NULL == self ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcSelf, rcNull );
    } else if ( self -> buffer_size < new_size ) {
        release_SBuffer( self );
        rc = make_SBuffer( self, new_size );
    }
    return rc;
}

rc_t print_to_SBufferV( SBuffer_t * self, const char * fmt, va_list args ) {
    rc_t rc = 0;
    if ( NULL == self ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcSelf, rcNull );
    } else {
        char * dst = ( char * )( self -> S . addr );
        size_t num_writ = 0;

        rc = string_vprintf( dst, self -> buffer_size, &num_writ, fmt, args );
        if ( 0 == rc ) {
            self -> S . size = num_writ;
            self -> S . len = ( uint32_t )self -> S . size;
        }
    }
    return rc;
}

rc_t try_to_enlarge_SBuffer( SBuffer_t * self, rc_t rc_err ) {
    rc_t rc = rc_err;
    if ( ( GetRCObject( rc ) == ( enum RCObject )rcBuffer ) && ( GetRCState( rc ) == rcInsufficient ) )
    {
        rc = increase_SBuffer( self, self -> buffer_size ); /* double it's size */
        if ( 0 != rc ) {
            ErrMsg( "try_to_enlarge_SBuffer().increase_SBuffer() -> %R", rc );
        }
    }
    return rc;
}

rc_t print_to_SBuffer( SBuffer_t * self, const char * fmt, ... ) {
    rc_t rc = 0;
    bool done = false;
    while ( 0 == rc && !done ) {
        va_list args;

        va_start( args, fmt );
        rc = print_to_SBufferV( self, fmt, args );
        va_end( args );

        done = ( 0 == rc );
        if ( !done ) {
            rc = try_to_enlarge_SBuffer( self, rc );
        }
    }
    return rc;
}

rc_t make_and_print_to_SBuffer( SBuffer_t * self, size_t len, const char * fmt, ... ) {
    rc_t rc = make_SBuffer( self, len );
    if ( 0 == rc ) {
        va_list args;

        va_start( args, fmt );
        rc = print_to_SBufferV( self, fmt, args );
        va_end( args );
    }
    return rc;
}

rc_t split_string( String * in, String * p0, String * p1, uint32_t ch ) {
    rc_t rc = 0;
    char * ch_ptr = string_chr( in -> addr, in -> size, ch );
    if ( NULL == ch_ptr ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcTransfer, rcInvalid );
    } else {
        p0 -> addr = in -> addr;
        p0 -> size = ( ch_ptr - p0 -> addr );
        p0 -> len  = ( uint32_t ) p0 -> size;
        p1 -> addr = ch_ptr + 1;
        p1 -> size = in -> len - ( p0 -> len + 1 );
        p1 -> len  = ( uint32_t ) p1 -> size;
    }
    return rc;
}

rc_t split_string_r( String * in, String * p0, String * p1, uint32_t ch ) {
    rc_t rc = 0;
    char * ch_ptr = string_rchr( in -> addr, in -> size, ch );
    if ( NULL == ch_ptr ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcTransfer, rcInvalid );
    } else {
        p0 -> addr = in -> addr;
        p0 -> size = ( ch_ptr - p0 -> addr );
        p0 -> len  = ( uint32_t ) p0 -> size;
        p1 -> addr = ch_ptr + 1;
        p1 -> size = in -> len - ( p0 -> len + 1 );
        p1 -> len  = ( uint32_t ) p1 -> size;
    }
    return rc;
}

rc_t split_filename_insert_idx( SBuffer_t * dst, size_t dst_size,
                                const char * filename, uint32_t idx ) {
    rc_t rc;
    if ( idx > 0 ) {
        /* we have to split md -> cmn -> output_filename into name and extension
           then append '_%u' to the name, then re-append the extension */
        String S_in, S_name, S_ext;
        StringInitCString( &S_in, filename );
        rc = split_string_r( &S_in, &S_name, &S_ext, '.' ); /* helper.c */
        if ( 0 == rc ) {
            /* we found a dot to split the filename! */
            rc = make_and_print_to_SBuffer( dst, dst_size, "%S_%u.%S",
                        &S_name, idx, &S_ext ); /* helper.c */
        } else {
            /* we did not find a dot to split the filename! */
            rc = make_and_print_to_SBuffer( dst, dst_size, "%s_%u.fastq",
                        filename, idx ); /* helper.c */
        }
    } else {
        rc = make_and_print_to_SBuffer( dst, dst_size, "%s", filename ); /* helper.c */
    }

    if ( 0 != rc ) {
        release_SBuffer( dst );
    }
    return rc;
}

compress_t get_compress_t( bool gzip, bool bzip2 ) {
    if ( gzip && bzip2 ) {
        return ct_bzip2;
    } else if ( gzip ) {
        return ct_gzip;
    } else if ( bzip2 ) {
        return ct_bzip2;
    }
    return ct_none;
}

uint64_t make_key( int64_t seq_spot_id, uint32_t seq_read_id ) {
    uint64_t key = seq_spot_id;
    key <<= 1;
    key |= ( 2 == seq_read_id ) ? 1 : 0;
    return key;
}

rc_t pack_4na( const String * unpacked, SBuffer_t * packed ) {
    rc_t rc = 0;
    if ( unpacked -> len < 1 ) {
        rc = RC( rcVDB, rcNoTarg, rcWriting, rcFormat, rcNull );
    } else {
        if ( unpacked -> len > 0xFFFF ) {
            rc = RC( rcVDB, rcNoTarg, rcWriting, rcFormat, rcExcessive );
        } else {
            uint32_t i;
            uint8_t * src = ( uint8_t * )unpacked -> addr;
            uint8_t * dst = ( uint8_t * )packed -> S . addr;
            uint16_t dna_len = ( unpacked -> len & 0xFFFF );
            uint32_t len = 0;
            dst[ len++ ] = ( dna_len >> 8 );
            dst[ len++ ] = ( dna_len & 0xFF );
            for ( i = 0; i < unpacked -> len; ++i ) {
                if ( len < packed -> buffer_size ) {
                    uint8_t base = ( src[ i ] & 0x0F );
                    if ( 0 == ( i & 0x01 ) ) {
                        dst[ len ] = ( base << 4 );
                    } else {
                        dst[ len++ ] |= base;
                    }
                }
            }
            if ( unpacked -> len & 0x01 ) {
                len++;
            }
            packed -> S . size = packed -> S . len = len;
        }
    }
    return rc;
}

static const char xASCII_to_4na[ 256 ] = {
    /* 0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x10 0x11 0x12 0x13 0x14 0x15 0x16 0x17 0x18 0x19 0x1A 0x1B 0x1C 0x1D 0x1E 0x1F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x20 0x21 0x22 0x23 0x24 0x25 0x26 0x27 0x28 0x29 0x2A 0x2B 0x2C 0x2D 0x2E 0x2F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x30 0x31 0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39 0x3A 0x3B 0x3C 0x3D 0x3E 0x3F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x40 0x41 0x42 0x43 0x44 0x45 0x46 0x47 0x48 0x49 0x4A 0x4B 0x4C 0x4D 0x4E 0x4F */
    /* @    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O */
       0,   1,   0,   2,   0,   0,   0,   4,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x50 0x51 0x52 0x53 0x54 0x55 0x56 0x57 0x58 0x59 0x5A 0x5B 0x5C 0x5D 0x5E 0x5F */
    /* P    Q    R    S    T    U    V    W    X    Y    Z    [    \    ]    ^    _ */
       0,   0,   0,   0,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x60 0x61 0x62 0x63 0x64 0x65 0x66 0x67 0x68 0x69 0x6A 0x6B 0x6C 0x6D 0x6E 0x6F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x70 0x71 0x72 0x73 0x74 0x75 0x76 0x77 0x78 0x79 0x7A 0x7B 0x7C 0x7D 0x7E 0x7F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x80 0x81 0x82 0x83 0x84 0x85 0x86 0x87 0x88 0x89 0x8A 0x8B 0x8C 0x8D 0x8E 0x8F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0x90 0x91 0x92 0x93 0x94 0x95 0x96 0x97 0x98 0x99 0x9A 0x9B 0x9C 0x9D 0x9E 0x9F */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0xA0 0xA1 0xA2 0xA3 0xA4 0xA5 0xA6 0xA7 0xA8 0xA9 0xAA 0xAB 0xAC 0xAD 0xAE 0xAF */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0xB0 0xB1 0xB2 0xB3 0xB4 0xB5 0xB6 0xB7 0xB8 0xB9 0xBA 0xBB 0xBC 0xBD 0xBE 0xBF */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0xC0 0xC1 0xC2 0xC3 0xC4 0xC5 0xC6 0xC7 0xC8 0xC9 0xCA 0xCB 0xCC 0xCD 0xCE 0xCF */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0xD0 0xD1 0xD2 0xD3 0xD4 0xD5 0xD6 0xD7 0xD8 0xD9 0xDA 0xDB 0xDC 0xDD 0xDE 0xDF */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0xE0 0xE1 0xE2 0xE3 0xE4 0xE5 0xE6 0xE7 0xE8 0xE9 0xEA 0xEB 0xEC 0xED 0xEE 0xEF */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,

    /* 0xF0 0xF1 0xF2 0xF3 0xF4 0xF5 0xF6 0xF7 0xF8 0xF9 0xFA 0xFB 0xFC 0xFD 0xFE 0xFF */
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

rc_t pack_read_2_4na( const String * read, SBuffer_t * packed ) {
    rc_t rc = 0;
    if ( read -> len < 1 ) {
        rc = RC( rcVDB, rcNoTarg, rcWriting, rcFormat, rcNull );
    } else {
        if ( read -> len > 0xFFFF ) {
            rc = RC( rcVDB, rcNoTarg, rcWriting, rcFormat, rcExcessive );
        } else {
            uint32_t i;
            uint8_t * src = ( uint8_t * )read -> addr;
            uint8_t * dst = ( uint8_t * )packed -> S . addr;
            uint16_t dna_len = ( read -> len & 0xFFFF );
            uint32_t len = 0;
            dst[ len++ ] = ( dna_len >> 8 );
            dst[ len++ ] = ( dna_len & 0xFF );
            for ( i = 0; i < read -> len; ++i ) {
                if ( len < packed -> buffer_size ) {
                    uint8_t base = ( xASCII_to_4na[ src[ i ] ] & 0x0F );
                    if ( 0 == ( i & 0x01 ) ) {
                        dst[ len ] = ( base << 4 );
                    } else {
                        dst[ len++ ] |= base;
                    }
                }
            }
            if ( read -> len & 0x01 ) {
                len++;
            }
            packed -> S . size = packed -> S . len = len;
        }
    }
    return rc;
}

static const char x4na_to_ASCII_fwd[ 16 ] = {
    /* 0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F */
       'N', 'A', 'C', 'N', 'G', 'N', 'N', 'N', 'T', 'N', 'N', 'N', 'N', 'N', 'N', 'N'
};

static const char x4na_to_ASCII_rev[ 16 ] = {
    /* 0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F */
       'N', 'T', 'G', 'N', 'C', 'N', 'N', 'N', 'A', 'N', 'N', 'N', 'N', 'N', 'N', 'N'
};

rc_t unpack_4na( const String * packed, SBuffer_t * unpacked, bool reverse ) {
    rc_t rc = 0;
    uint8_t * src = ( uint8_t * )packed -> addr;
    uint16_t dna_len;

    /* the first 2 bytes are the 16-bit dna-length */
    dna_len = src[ 0 ];
    dna_len <<= 8;
    dna_len |= src[ 1 ];

    if ( dna_len > unpacked -> buffer_size ) {
        rc = increase_SBuffer( unpacked, dna_len - unpacked -> buffer_size );
    }
    if ( 0 == rc ) {
        uint8_t * dst = ( uint8_t * )unpacked -> S . addr;
        uint32_t dst_idx;
        uint32_t i;

        /* use the complement-lookup-table in case of reverse */
        const char * lookup = reverse ? x4na_to_ASCII_rev : x4na_to_ASCII_fwd;

        dst_idx = reverse ? dna_len - 1 : 0;

        for ( i = 2; i < packed -> len; ++i ) {
            /* get the packed byte out of the packed input */
            uint8_t packed_byte = src[ i ];

            /* write the first unpacked byte */
            if ( dst_idx < unpacked -> buffer_size ) {
                dst[ dst_idx ] = lookup[ ( packed_byte >> 4 ) & 0x0F ];
                dst_idx += reverse ? -1 : 1;
            }

            /* write the second unpacked byte */
            if ( dst_idx < unpacked -> buffer_size ) {
                dst[ dst_idx ] = lookup[ packed_byte & 0x0F ];
                dst_idx += reverse ? -1 : 1;
            }
        }

        /* set the dna-length in the output-string */
        unpacked -> S . size = dna_len;
        unpacked -> S . len = ( uint32_t )unpacked -> S . size;

        /* terminated the output-string, just in case */
        dst[ dna_len ] = 0;
    }
    return rc;
}

bool check_expected( const KDirectory * dir, uint32_t expected, const char * fmt, va_list args ) {
    bool res = false;
    char buffer[ 4096 ];
    size_t num_writ;
    
    rc_t rc = string_vprintf( buffer, sizeof buffer, &num_writ, fmt, args );
    if ( 0 == rc ) {
        uint32_t pt = KDirectoryPathType( dir, "%s", buffer );
        res = ( pt == expected );
    }
    return res;
}

bool file_exists( const KDirectory * dir, const char * fmt, ... ) {
    bool res = false;
    va_list args;
    va_start( args, fmt );
    res = check_expected( dir, kptFile, fmt, args ); /* because KDirectoryPathType() uses vsnprintf */
    va_end( args );
    return res;
}

bool dir_exists( const KDirectory * dir, const char * fmt, ... ) {
    bool res = false;
    va_list args;
    va_start( args, fmt );
    res = check_expected( dir, kptDir, fmt, args ); /* because KDirectoryPathType() uses vsnprintf */
    va_end( args );
    return res;
}

rc_t join_and_release_threads( Vector * threads ) {
    rc_t rc = 0;
    uint32_t i, n = VectorLength( threads );
    for ( i = VectorStart( threads ); i < n; ++i ) {
        KThread * thread = VectorGet( threads, i );
        if ( NULL != thread ) {
            rc_t rc1;
            KThreadWait( thread, &rc1 );
            if ( 0 == rc && rc1 != 0 ) {
                rc = rc1;
            }
            KThreadRelease( thread );
        }
    }
    VectorWhack ( threads, NULL, NULL );
    return rc;
}

void clear_join_stats( join_stats_t * stats ) {
    if ( stats != NULL )
    {
        stats -> spots_read = 0;
        stats -> reads_read = 0;
        stats -> reads_written = 0;
        stats -> reads_zero_length = 0;
        stats -> reads_technical = 0;
        stats -> reads_too_short = 0;
        stats -> reads_invalid = 0;
    }
}

void add_join_stats( join_stats_t * stats, const join_stats_t * to_add ) {
    if ( NULL != stats && NULL != to_add )
    {
        stats -> spots_read += to_add -> spots_read;
        stats -> reads_read += to_add -> reads_read;
        stats -> reads_written += to_add -> reads_written;
        stats -> reads_zero_length += to_add -> reads_zero_length;
        stats -> reads_technical += to_add -> reads_technical;
        stats -> reads_too_short += to_add -> reads_too_short;
        stats -> reads_invalid += to_add -> reads_invalid;
    }
}

rc_t delete_files( KDirectory * dir, const VNamelist * files ) {
    uint32_t count;
    rc_t rc = VNameListCount( files, &count );
    if ( 0 != rc ) {
        ErrMsg( "delete_files().VNameListCount() -> %R", rc );
    } else {
        uint32_t idx;
        for ( idx = 0; 0 == rc && idx < count; ++idx ) {
            const char * filename;
            rc = VNameListGet( files, idx, &filename );
            if ( 0 != rc ) {
                ErrMsg( "delete_files.VNameListGet( #%d ) -> %R", idx, rc );
            } else {
                if ( file_exists( dir, "%s", filename ) ) {
                    rc = KDirectoryRemove( dir, true, "%s", filename );
                    if ( 0 != rc ) {
                        ErrMsg( "delete_files.KDirectoryRemove( '%s' ) -> %R", filename, rc );
                    }
                }
            }
        }
    }
    return rc;
}

rc_t delete_dirs( KDirectory * dir, const VNamelist * dirs ) {
    uint32_t count;
    rc_t rc = VNameListCount( dirs, &count );
    if ( 0 != rc ) {
        ErrMsg( "delete_dirs().VNameListCount() -> %R", rc );
    } else {
        uint32_t idx;
        for ( idx = 0; 0 == rc && idx < count; ++idx ) {
            const char * dirname;
            rc = VNameListGet( dirs, idx, &dirname );
            if ( 0 != rc ) {
                ErrMsg( "delete_dirs().VNameListGet( #%d ) -> %R", idx, rc );
            } else if ( dir_exists( dir, "%s", dirname ) ) {
                rc = KDirectoryClearDir ( dir, true, "%s", dirname );
                if ( 0 != rc ) {
                    ErrMsg( "delete_dirs().KDirectoryClearDir( %s ) -> %R", dirname, rc );
                } else {
                    rc = KDirectoryRemove ( dir, true, "%s", dirname );
                    if ( 0 != rc ) {
                        ErrMsg( "delete_dirs().KDirectoryRemove( %s ) -> %R", dirname, rc );
                    }
                }
            }
        }
    }
    return rc;
}

uint64_t total_size_of_files_in_list( KDirectory * dir, const VNamelist * files ) {
    uint64_t res = 0;
    if ( NULL != dir && NULL != files ) {
        uint32_t idx, count;
        rc_t rc = VNameListCount( files, &count );
        if ( 0 != rc ) {
            ErrMsg( "total_size_of_files_in_list().VNameListCount() -> %R", rc );
        } else {
            for ( idx = 0; 0 == rc && idx < count; ++idx ) {
                const char * filename;
                rc = VNameListGet( files, idx, &filename );
                if ( 0 != rc ) {
                    ErrMsg( "total_size_of_files_in_list().VNameListGet( #%d ) -> %R", idx, rc );
                } else {
                    uint64_t size;
                    rc_t rc1 = KDirectoryFileSize( dir, &size, "%s", filename );
                    if ( 0 != rc1 ) {
                        ErrMsg( "total_size_of_files_in_list().KDirectoryFileSize( %s ) -> %R", filename, rc );
                    } else {
                        res += size;
                    }
                }
            }
        }
    }
    return res;
}

/*
int get_vdb_pathtype( KDirectory * dir, const VDBManager * vdb_mgr, const char * accession ) {
    int res = kptAny;
    rc_t rc = 0;
    bool release_mgr = false;
    const VDBManager * mgr = vdb_mgr != NULL ? vdb_mgr : NULL;
    if ( mgr == NULL ) {
        rc = VDBManagerMakeRead( &mgr, dir );
        if ( rc != 0 ) {
            ErrMsg( "get_vdb_pathtype().VDBManagerMakeRead() -> %R\n", rc );
        } else {
            release_mgr = true;
        }
    }
    if ( rc == 0 ) {
        res = ( VDBManagerPathType ( mgr, "%s", accession ) & ~ kptAlias );
        if ( release_mgr ) {
            VDBManagerRelease( mgr );
        }
    }
    return res;
}
*/

/* ===================================================================================== */

bool ends_in_slash( const char * s ) {
    bool res = false;
    if ( NULL != s ) {
        uint32_t len = string_measure( s, NULL );
        if ( len > 0 ) {
            res = ( '/' == s[ len - 1 ] );
        }
    }
    return res;
}

static bool ends_in_sra( const char * s ) {
    bool res = false;
    if ( NULL != s ) {
        uint32_t len = string_measure( s, NULL );
        if ( len > 4 ) {
            res = ( ( 'a' == s[ len - 1 ] ) &&
                    ( 'r' == s[ len - 2 ] ) &&
                    ( 's' == s[ len - 3 ] ) &&
                    ( '.' == s[ len - 4 ] ) );
        }
    }
    return res;
}

bool extract_path( const char * s, String * path ) {
    bool res = false;
    if ( NULL != s && NULL != path ) {
        path -> addr = s;
        res = ends_in_slash( s );
        if ( res ) {
            path -> len = string_measure( s, & path -> size );
        } else {
            size_t size = string_size ( s );
            char * slash = string_rchr ( s, size, '/' );
            res = ( NULL != slash );
            if ( res ) {
                path -> len = ( slash - s );
                path -> size = ( size_t ) path -> len;
            }
        }
    }
    return res;
}

const char * extract_acc( const char * s ) {
    const char * res = NULL;
    if ( ( NULL != s ) && ( !ends_in_slash( s ) ) ) {
        size_t size = string_size ( s );
        char * slash = string_rchr ( s, size, '/' );
        if ( NULL == slash ) {
            if ( ends_in_sra( s ) ) {
                res = string_dup ( s, size - 4 );
            } else {
                res = string_dup ( s, size );
            }
        } else {
            char * tmp = slash + 1;
            if ( ends_in_sra( tmp ) ) {
                res = string_dup ( tmp, string_size ( tmp ) - 4 );
            } else {
                res = string_dup ( tmp, string_size ( tmp ) );
            }
        }
    }
    return res;
}

const char * extract_acc2( const char * s ) {
    const char * res = NULL;
    VFSManager * mgr;
    rc_t rc = VFSManagerMake ( &mgr );
    if ( 0 != rc ) {
        ErrMsg( "extract_acc2( '%s' ).VFSManagerMake() -> %R", s, rc );
    } else {
        VPath * orig;
        rc = VFSManagerMakePath ( mgr, &orig, "%s", s );
        if ( 0 != rc ) {
            ErrMsg( "extract_acc2( '%s' ).VFSManagerMakePath() -> %R", s, rc );
        } else {
            VPath * acc_or_oid = NULL;
            rc = VFSManagerExtractAccessionOrOID( mgr, &acc_or_oid, orig );
            if ( 0 != rc ) { /* remove trailing slash[es] and try again */
                char P_option_buffer[ PATH_MAX ] = "";
                size_t l = string_copy_measure( P_option_buffer, sizeof P_option_buffer, s );
                char * basename = P_option_buffer;
                while ( l > 0 && strchr( "\\/", basename[ l - 1 ]) != NULL ) {
                    basename[ --l ] = '\0';
                }
                VPath * orig = NULL;
                rc = VFSManagerMakePath ( mgr, &orig, "%s", P_option_buffer );
                if ( 0 != rc ) {
                    ErrMsg( "extract_acc2( '%s' ).VFSManagerMakePath() -> %R", P_option_buffer, rc );
                } else {
                    rc = VFSManagerExtractAccessionOrOID( mgr, &acc_or_oid, orig );
                    if ( 0 != rc ) {
                        ErrMsg( "extract_acc2( '%s' ).VFSManagerExtractAccessionOrOID() -> %R", s, rc );
                    }
                    {
                        rc_t r2 = VPathRelease ( orig );
                        if ( 0 != r2 ) {
                            ErrMsg( "extract_acc2( '%s' ).VPathRelease().2 -> %R", P_option_buffer, rc );
                            if ( 0 == rc ) { rc = r2; }
                        }
                    }
                }
            }
            if ( 0 == rc ) {
                char buffer[ 1024 ];
                size_t num_read;
                rc = VPathReadPath ( acc_or_oid, buffer, sizeof buffer, &num_read );
                if ( 0 != rc ) {
                    ErrMsg( "extract_acc2( '%s' ).VPathReadPath() -> %R", s, rc );
                } else {
                    res = string_dup ( buffer, num_read );
                }
                rc = VPathRelease ( acc_or_oid );
                if ( 0 != rc ) {
                    ErrMsg( "extract_acc2( '%s' ).VPathRelease().1 -> %R", s, rc );
                }
            }

            rc = VPathRelease ( orig );
            if ( 0 != rc ) {
                ErrMsg( "extract_acc2( '%s' ).VPathRelease().2 -> %R", s, rc );
            }
        }

        rc = VFSManagerRelease ( mgr );
        if ( 0 != rc ) {
            ErrMsg( "extract_acc2( '%s' ).VFSManagerRelease() -> %R", s, rc );
        }
    }
    return res;
}

rc_t create_this_file( KDirectory * dir, const char * filename, bool force ) {
    struct KFile * f;
    KCreateMode create_mode = force ? kcmInit : kcmCreate;
    rc_t rc = KDirectoryCreateFile( dir, &f, false, 0664, create_mode | kcmParents, "%s", filename );
    if ( 0 != rc ) {
        ErrMsg( "create_this_file().KDirectoryCreateFile( '%s' ) -> %R", filename, rc );
    } else {
        rc_t rc2 = KFileRelease( f );
        if ( 0 != rc2 ) {
            ErrMsg( "create_this_file( '%s' ).KFileRelease() -> %R", filename, rc2 );
            rc = ( 0 == rc ) ? rc2 : rc;
        }
    }
    return rc;
}

rc_t create_this_dir( KDirectory * dir, const String * dir_name, bool force ) {
    KCreateMode create_mode = force ? kcmInit : kcmCreate;
    rc_t rc = KDirectoryCreateDir( dir, 0774, create_mode | kcmParents, "%.*s", dir_name -> len, dir_name -> addr );
    if ( 0 != rc ) {
        ErrMsg( "create_this_dir().KDirectoryCreateDir( '%.*s' ) -> %R", dir_name -> len, dir_name -> addr, rc );
    }
    return rc;
}

rc_t create_this_dir_2( KDirectory * dir, const char * dir_name, bool force ) {
    KCreateMode create_mode = force ? kcmInit : kcmCreate;
    rc_t rc = KDirectoryCreateDir( dir, 0774, create_mode | kcmParents, "%s", dir_name );
    if ( 0 != rc ) {
        ErrMsg( "create_this_dir_2().KDirectoryCreateDir( '%s' ) -> %R", dir_name, rc );
    }
    return rc;
}

rc_t make_buffered_for_read( KDirectory * dir, const struct KFile ** f,
                             const char * filename, size_t buf_size ) {
    const struct KFile * fr;
    rc_t rc = KDirectoryOpenFileRead( dir, &fr, "%s", filename );
    if ( 0 != rc ) {
        ErrMsg( "make_buffered_for_read().KDirectoryOpenFileRead( '%s' ) -> %R", filename, rc );
    } else {
        if ( buf_size > 0 ) {
            const struct KFile * fb;
            rc = KBufFileMakeRead( &fb, fr, buf_size );
            if ( 0 != rc ) {
                ErrMsg( "make_buffered_for_read( '%s' ).KBufFileMakeRead() -> %R", filename, rc );
            } else {
                rc = KFileRelease( fr );
                if ( 0 != rc ) {
                    ErrMsg( "make_buffered_for_read( '%s' ).KFileRelease().1 -> %R", filename, rc );
                } else {
                    fr = fb;
                }
            }
        }
        if ( 0 == rc ) {
            *f = fr;
        } else {
            rc_t rc2 = KFileRelease( fr );
            if ( 0 != rc2 ) {
                ErrMsg( "make_buffered_for_read( '%s' ).KFileRelease().2 -> %R", filename, rc2 );
            }
        }
    }
    return rc;
}

/* ===================================================================================== */

rc_t locked_file_list_init( locked_file_list_t * self, uint32_t alloc_blocksize ) {
    rc_t rc;
    if ( NULL == self || 0 == alloc_blocksize ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_file_list_init() -> %R", rc );
    } else {
        rc = KLockMake ( &( self -> lock ) );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_init().KLockMake() -> %R", rc );
        } else {
            rc = VNamelistMake ( & self -> files, alloc_blocksize );
            if ( 0 != rc ) {
                ErrMsg( "locked_file_list_init().VNamelistMake() -> %R", rc );
            }
        }
    }
    return rc;
}

rc_t locked_file_list_release( locked_file_list_t * self, KDirectory * dir ) {
    rc_t rc = 0;
    /* tolerates to be called with self == NULL */
    if ( NULL != self ) {
        rc = KLockRelease ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_release().KLockRelease() -> %R", rc );
        } else {
            /* tolerates to be called with dir == NULL */
            if ( NULL != dir ) {
                rc = delete_files( dir, self -> files );
            }
        }
        {
            rc_t rc2 = VNamelistRelease ( self -> files );
            if ( 0 != rc2 ) {
                ErrMsg( "locked_file_list_release().VNamelistRelease() -> %R", rc );
            }
        }
    }
    return rc;
}

static rc_t locked_file_list_unlock( const locked_file_list_t * self, const char * function, rc_t rc ) {
    rc_t rc2 = KLockUnlock ( self -> lock );
    if ( 0 != rc2 ) {
        ErrMsg( "%s().KLockUnlock() -> %R", function, rc2 );
        rc = ( 0 == rc ) ? rc2 : rc;
    }
    return rc;
}

rc_t locked_file_list_append( const locked_file_list_t * self, const char * filename ) {
    rc_t rc = 0;
    if ( NULL == self || NULL == filename ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_file_list_append() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_append( '%s' ).KLockAcquire() -> %R", filename, rc );
        } else {
            rc = VNamelistAppend ( self -> files, filename );
            if ( 0 != rc ) {
                ErrMsg( "locked_file_list_append( '%s' ).VNamelistAppend() -> %R", filename, rc );
            }
            rc = locked_file_list_unlock( self, "locked_file_list_append", rc );
        }
    }
    return rc;
}

rc_t locked_file_list_delete_files( KDirectory * dir, locked_file_list_t * self ) {
    rc_t rc = 0;
    if ( NULL == self || NULL == dir ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_file_list_delete_files() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_delete_files().KLockAcquire() -> %R", rc );
        } else {
            rc = delete_files( dir, self -> files );
            if ( 0 != rc ) {
                ErrMsg( "locked_file_list_delete_files().delete_files() -> %R", rc );
            }
            rc = locked_file_list_unlock( self, "locked_file_list_delete_files", rc );
        }
    }
    return rc;
}

rc_t locked_file_list_delete_dirs( KDirectory * dir, locked_file_list_t * self ) {
    rc_t rc = 0;
    if ( NULL == self || NULL == dir ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_file_list_delete_dirs() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_delete_dirs().KLockAcquire() -> %R", rc );
        } else {
            rc = delete_dirs( dir, self -> files );
            if ( 0 != rc ) {
                ErrMsg( "locked_file_list_delete_dirs().delete_dirs() -> %R", rc );
            }
            rc = locked_file_list_unlock( self, "locked_file_list_delete_dirs", rc );
        }
    }
    return rc;
}

rc_t locked_file_list_count( const locked_file_list_t * self, uint32_t * count ) {
    rc_t rc = 0;
    if ( NULL == self || NULL == count ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_file_list_delete_dirs() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_count().KLockAcquire() -> %R", rc );
        } else {
            rc = VNameListCount( self -> files, count );
            if ( 0 != rc ) {
                ErrMsg( "locked_file_list_count().VNameListCount() -> %R", rc );
            }
            rc = locked_file_list_unlock( self, "locked_file_list_count", rc );
        }
    }
    return rc;
}

rc_t locked_file_list_pop( locked_file_list_t * self, const String ** item ) {
    rc_t rc = 0;
    if ( NULL == self || NULL == item ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_file_list_pop() -> %R", rc );
    } else {
        *item = NULL;
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_file_list_pop().KLockAcquire() -> %R", rc );
        } else {
            const char * s;
            rc = VNameListGet ( self -> files, 0, &s );
            if ( 0 != rc ) {
                ErrMsg( "locked_file_list_pop().VNameListGet() -> %R", rc );
            } else {
                String S;
                StringInitCString( &S, s );
                rc = StringCopy ( item, &S );
                if ( 0 != rc ) {
                    ErrMsg( "locked_file_list_pop().StringCopy() -> %R", rc );
                } else {
                    rc = VNamelistRemoveIdx( self -> files, 0 );
                    if ( 0 != rc ) {
                        ErrMsg( "locked_file_list_pop().VNamelistRemoveIdx() -> %R", rc );
                    }
                }
            }
            rc = locked_file_list_unlock( self, "locked_file_list_pop", rc );
        }
    }
    return rc;
}

/* ===================================================================================== */

rc_t locked_vector_init( locked_vector_t * self, uint32_t alloc_blocksize ) {
    rc_t rc;
    if ( NULL == self || 0 == alloc_blocksize ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_vector_init() -> %R", rc );
    } else {
        rc = KLockMake ( &( self -> lock ) );
        if ( 0 != rc ) {
            ErrMsg( "locked_vector_init().KLockMake() -> %R", rc );
        } else {
            VectorInit ( &( self -> vector ), 0, alloc_blocksize );
            self -> sealed = false;
        }
    }
    return rc;
}

static rc_t locked_vector_unlock( const locked_vector_t * self, const char * function, rc_t rc ) {
    rc_t rc2 = KLockUnlock ( self -> lock );
    if ( 0 != rc2 ) {
        ErrMsg( "%s().KLockUnlock() -> %R", function, rc2 );
        rc = ( 0 == rc ) ? rc2 : rc;
    }
    return rc;
}

void locked_vector_release( locked_vector_t * self,
                            void ( CC * whack ) ( void *item, void *data ), void *data ) {
    if ( NULL == self ) {
        rc_t rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_vector_release().KLockAcquire() -> %R", rc );
        } else {
            VectorWhack ( &( self -> vector ), whack, data );
            rc = locked_vector_unlock( self, "locked_vector_release", rc );
        }
        rc = KLockRelease ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_vector_release().KLockRelease() -> %R", rc );
        }
    }
}

rc_t locked_vector_push( locked_vector_t * self, const void * item, bool seal ) {
    rc_t rc;
    if ( NULL == self || NULL == item ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_vector_push() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_vector_push().KLockAcquire -> %R", rc );
        } else {
            rc = VectorAppend ( &( self -> vector ), NULL, item );
            if ( 0 != rc ) {
                ErrMsg( "locked_vector_push().VectorAppend -> %R", rc );
            }
            if ( seal ) {
                self -> sealed = true;
            }
            rc = locked_vector_unlock( self, "locked_vector_push", rc );
        }
    }
    return rc;
}

rc_t locked_vector_pop( locked_vector_t * self, void ** item, bool * sealed ) {
    rc_t rc;
    if ( NULL == self || NULL == item || NULL == sealed ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_vector_pop() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_vector_pop().KLockAcquire -> %R", rc );
        } else {
            if ( 0 == VectorLength( &( self -> vector ) ) ) {
                rc = 0;
                *sealed = self -> sealed;
                *item = NULL;
            } else {
                *sealed = false;
                rc = VectorRemove ( &( self -> vector ), 0, item );
                if ( 0 != rc ) {
                    ErrMsg( "locked_vector_pop().VectorRemove -> %R", rc );
                }
            }
            rc = locked_vector_unlock( self, "locked_vector_pop", rc );
        }
    }
    return rc;
}

/* ===================================================================================== */

rc_t locked_value_init( locked_value_t * self, uint64_t init_value ) {
    rc_t rc;
    if ( NULL == self ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_value_init() -> %R", rc );
    } else {
        rc = KLockMake ( &( self -> lock ) );
        if ( 0 != rc ) {
            ErrMsg( "locked_value_init().KLockMake() -> %R", rc );
        } else {
            self -> value = init_value;
        }
    }
    return rc;
}

void locked_value_release( locked_value_t * self ) {
    if ( NULL != self ) {
        rc_t rc = KLockRelease ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_value_init().KLockRelease() -> %R", rc );
        }
    }
}

rc_t locked_value_get( locked_value_t * self, uint64_t * value ) {
    rc_t rc;
    if ( NULL == self || NULL == value ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_value_get() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_value_get().KLockAcquire -> %R", rc );
        } else {
            *value = self -> value;
            rc = KLockUnlock ( self -> lock );
            if ( 0 != rc ) {
                ErrMsg( "locked_value_get().KLockUnlock -> %R", rc );
            }
        }
    }
    return rc;
}

rc_t locked_value_set( locked_value_t * self, uint64_t value ) {
    rc_t rc;
    if ( NULL == self ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "locked_value_set() -> %R", rc );
    } else {
        rc = KLockAcquire ( self -> lock );
        if ( 0 != rc ) {
            ErrMsg( "locked_value_set().KLockAcquire -> %R", rc );
        } else {
            self -> value = value;
            rc = KLockUnlock ( self -> lock );
            if ( 0 != rc ) {
                ErrMsg( "locked_value_set().KLockUnlock -> %R", rc );
            }
        }
    }
    return rc;
}

/* ===================================================================================== */

typedef struct Buf2NA_t {
    unsigned char map [ 1 << ( sizeof ( char ) * 8 ) ];
    size_t shiftLeft[ 4 ];
    NucStrstr * nss;
    uint8_t * buffer;
    size_t allocated;
} Buf2NA_t;

rc_t make_Buf2NA( Buf2NA_t ** self, size_t size, const char * pattern ) {
    rc_t rc = 0;
    NucStrstr * nss;
    int res = NucStrstrMake ( &nss, 0, pattern, string_size ( pattern ) );
    if ( 0 != res ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcParam, rcInvalid );
        ErrMsg( "make_Buf2NA().NucStrstrMake() -> %R", rc );
    } else {
        uint8_t * buffer = calloc( size, sizeof * buffer );
        if ( NULL == buffer ) {
            rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
            ErrMsg( "make_Buf2NA().calloc().1() -> %R", rc );
            NucStrstrWhack ( nss );
        } else {
            Buf2NA_t * res = calloc( 1, sizeof * res );
            if ( NULL == res ) {
                rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
                ErrMsg( "make_Buf2NA().calloc().2() -> %R", rc );
                NucStrstrWhack ( nss );
                free( ( void * ) buffer );
            } else {
                res -> nss = nss;
                res -> buffer = buffer;
                res -> allocated = size;
                res -> map[ 'A' ] = res -> map[ 'a' ] = 0;
                res -> map[ 'C' ] = res -> map[ 'c' ] = 1;
                res -> map[ 'G' ] = res -> map[ 'g' ] = 2;
                res -> map[ 'T' ] = res -> map[ 't' ] = 3;
                res -> shiftLeft [ 0 ] = 6;
                res -> shiftLeft [ 1 ] = 4;
                res -> shiftLeft [ 2 ] = 2;
                res -> shiftLeft [ 3 ] = 0;
                *self = res;
            }
        }
    }
    return rc;
}

void release_Buf2NA( Buf2NA_t * self ) {
    if ( self != NULL ) {
        if ( self -> buffer != NULL ) {
            free( ( void * ) self -> buffer );
        }
        if ( self -> nss != NULL ) {
            NucStrstrWhack ( self -> nss );
        }
        free( ( void * ) self );
    }
}

bool match_Buf2NA( Buf2NA_t * self, const String * ascii ) {
    bool res = false;
    if ( self != NULL && ascii != NULL ) {
        int i;
        size_t needed = ( ( ascii -> len + 3 ) / 4 );
        if ( needed > self -> allocated ) {
            free( ( void * )self -> buffer );
            self -> buffer = calloc( needed, sizeof *( self -> buffer ) );
        }
        else {
            memset( self -> buffer, 0, needed );
        }
        if ( self -> buffer != NULL ) {
            unsigned int selflen;
            int dst = 0;
            int src = 0;
            i = ascii -> len;
            while ( i >= 4 ) {
                self -> buffer[ dst++ ] =
                    self -> map[ ( unsigned char )ascii -> addr[ src ] ] << 6 |
                    self -> map[ ( unsigned char )ascii -> addr[ src + 1 ] ] << 4 |
                    self -> map[ ( unsigned char )ascii -> addr[ src + 2 ] ] << 2 |
                    self -> map[ ( unsigned char )ascii -> addr[ src + 3 ] ];
                src += 4;
                i -= 4;
            }
            switch( i ) {
                case 3 : self -> buffer[ dst ] = 
                            self -> map[ ( unsigned char )ascii -> addr[ src ] ] << 6 |
                            self -> map[ ( unsigned char )ascii -> addr[ src + 1 ] ] << 4 |
                            self -> map[ ( unsigned char )ascii -> addr[ src + 2 ] ] << 2; break;
                case 2 : self -> buffer[ dst ] = 
                            self -> map[ ( unsigned char )ascii -> addr[ src ] ] << 6 |
                            self -> map[ ( unsigned char )ascii -> addr[ src + 1 ] ] << 4; break;
                case 1 : self -> buffer[ dst ] = 
                            self -> map[ ( unsigned char )ascii -> addr[ src ] ] << 6; break;
            }
            res = ( 0 != NucStrstrSearch ( self -> nss, self -> buffer, 0, ascii -> len, & selflen ) );
        }
    }
    return res;
}

rc_t helper_make_thread( KThread ** self,
                         rc_t ( CC * run_thread ) ( const KThread * self, void * data ),
                         void * data,
                         size_t stacksize ) {
    rc_t rc = KThreadMakeStackSize( self, run_thread, data, stacksize );
    return rc;
}

/* ===================================================================================== */
static atomic32_t quit_flag;

rc_t get_quitting( void ) {
    rc_t rc = Quitting();
    if ( 0 == rc ) {
        if ( 0 != atomic32_read ( & quit_flag ) )
        rc = RC ( rcExe, rcProcess, rcExecuting, rcProcess, rcCanceled );
    }
    return rc;
}

void set_quitting( void ) {
    atomic32_inc ( & quit_flag );
}

/* ===================================================================================== */

uint64_t calculate_rows_per_thread( uint32_t * num_threads, uint64_t row_count ) {
    uint64_t res = row_count;
    uint64_t limit = 100 * ( *num_threads );
    if ( row_count < limit ) {
        *num_threads = 1;
    } else {
        res = ( row_count / ( *num_threads ) ) + 1;
    }
    return res;
}

void correct_join_options( join_options_t * dst, const join_options_t * src, bool name_column_present ) {
    dst -> rowid_as_name = name_column_present ? src -> rowid_as_name : true;
    dst -> skip_tech = src -> skip_tech;
    dst -> print_read_nr = src -> print_read_nr;
    dst -> print_name = src -> print_name;
    dst -> print_spotgroup = src -> print_spotgroup;
    dst -> min_read_len = src -> min_read_len;
    dst -> filter_bases = src -> filter_bases;
    dst -> terminate_on_invalid = src -> terminate_on_invalid;
}

/* ===================================================================================== */

rc_t release_file( struct KFile * f, const char * err_msg ) {
    rc_t rc = KFileRelease( f );
    if ( 0 != rc ) {
        ErrMsg( "%s release_file() -> %R", err_msg, rc );
    }
    return rc;
}

rc_t wrap_file_in_buffer( struct KFile ** f, size_t buffer_size, const char * err_msg ) {
    struct KFile * temp_file = *f;
    rc_t rc = KBufFileMakeWrite( &temp_file, *f, false, buffer_size );
    if ( 0 != rc ) {
        ErrMsg( "%s KBufFileMakeWrite() -> %R", err_msg, rc );
    } else {
        rc = release_file( *f, err_msg );
        if ( 0 == rc ) { *f = temp_file; }
    }
    return rc;
}

/* ============================================================================================================= */
typedef enum var_fmt_type_t { vft_literal, vft_str, vft_int } var_fmt_type_t;
/* ============================================================================================================= */

/* ============================================================================================================= */
/* private: the format-elements = literal, String, uin64_t */
typedef struct var_desc_t {
    var_fmt_type_t type;    /* which one of the options is it? ( vft_literal not used ) */
    const String * name;    /* this one is owned by this struct! */
    uint8_t idx;            /* which idx to use ( for vft_str, vft_int ) */
    uint8_t idx2;           /* alternative idx ( for vft_str, if NULL-ptr given by client ) */
} var_desc_t;
/* ============================================================================================================= */

/* create a var-name-element, makes a copy of the given string and owns that copy */
static var_desc_t * var_desc_create( const char * src, var_fmt_type_t t, uint8_t idx, uint8_t idx2 ) {
    var_desc_t * res = calloc( 1, sizeof * res );
    if ( NULL != res ) {
        rc_t rc;
        String S;
        StringInitCString( &S, src );
        rc = StringCopy( &( res -> name ), &S );
        if ( 0 == rc ) {
            res -> type = t;
            res -> idx = idx;
            res -> idx2 = idx2;
        }
        else {
            free( ( void * ) res );
            res = NULL;
        }
    }
    return res;
}

static void destroy_var_desc( void * self, void * data ) {
    if ( NULL != self ) {
        var_desc_t * desc = ( var_desc_t * )self;
        StringWhack ( desc -> name );
        free( self );
    }
}

static int64_t var_desc_cmp( const void *key, const void *n ) {
    const String * s_key = ( String * ) key;
    var_desc_t * item = ( var_desc_t * ) n;
    if ( s_key -> len < item -> name -> len ) {
        return -1;
    }
    else {
        uint32_t item_len = item -> name -> len;
        int32_t offset = s_key -> len - item_len;
        String s_key2;
        StringInit( &s_key2, s_key -> addr + offset, item_len, item_len );
        return StringOrder( &s_key2, item -> name );
    }
}

typedef struct var_desc_list_t {
    Vector descriptions;        /* pointers to var_desc_t - structs */
} var_desc_list_t;

struct var_desc_list_t * create_var_desc_list( void ) {
    var_desc_list_t * res = calloc( 1, sizeof * res );
    if ( NULL != res ) {
        VectorInit ( &( res -> descriptions ), 0, 12 );
    }
    return res;
}

void release_var_desc_list( var_desc_list_t * self ) {
    if ( NULL != self ) {
        VectorWhack ( &( self -> descriptions ), destroy_var_desc, NULL );
        free( ( void * ) self );
    }
}

static void var_desc_list_add( var_desc_list_t * self, var_desc_t * desc ) {
    if ( NULL != desc ) {
        rc_t rc = VectorAppend ( &( self -> descriptions ), NULL, desc );
        if ( 0 != rc ) {
            destroy_var_desc( desc, NULL );
        }
    }
}

void var_desc_list_add_str( var_desc_list_t * self, const char * name, uint32_t idx, uint32_t idx2 ) {
    if ( NULL != self && NULL != name ) {
        var_desc_list_add( self, var_desc_create( name, vft_str, idx, idx2 ) );
    }
}

void var_desc_list_add_int( var_desc_list_t * self, const char * name, uint32_t idx ) {
    if ( NULL != self && NULL != name ) {
        var_desc_list_add( self, var_desc_create( name, vft_int, idx, 0xFF ) );
    }
}

static const var_desc_t * var_desc_list_find( const var_desc_list_t * self, const String * to_find ) {
    const var_desc_t * res = NULL;
    if ( NULL != self && NULL != to_find ) {
        const Vector * v =  &( self -> descriptions );
        uint32_t i, l = VectorLength( v );
        for ( i = VectorStart( v ); i < l && NULL == res; ++i ) {
            const var_desc_t * desc = VectorGet ( v, i );
            if ( NULL != desc ) {
                int64_t cmp = var_desc_cmp( to_find, desc );
                if ( 0 == cmp ) { res = desc; }
            }
        }
    }
    return res;
}

static void var_desc_test_find( var_desc_list_t * self, char * to_find ) {
    String S1;
    StringInitCString( &S1, to_find );
    const var_desc_t * desc = var_desc_list_find( self, &S1 );

    KOutMsg( "found( '%s' ) = %p\n", to_find, desc );
    if ( NULL != desc ) {
        KOutMsg( "\tname = %S, idx = %u, type = %u\n", desc -> name, desc -> idx, desc -> type );
    }
}

void var_desc_list_test( void ) {
    var_desc_list_t * lst = NULL;
    
    KOutMsg( "var-desc-list-test\n" );
    lst = create_var_desc_list();
    if ( NULL != lst ) {
        var_desc_list_add_str( lst, "$ac", 0, 0xFF );
        var_desc_list_add_str( lst, "$sg", 1, 0xFF );
        var_desc_list_add_int( lst, "$si", 0 );
        var_desc_list_add_int( lst, "$sl", 1 );

        var_desc_test_find( lst, "test" );
        var_desc_test_find( lst, "$ac" );
        var_desc_test_find( lst, "$sg" );
        var_desc_test_find( lst, "$si" );
        var_desc_test_find( lst, "$sl" );
        var_desc_test_find( lst, "xyz$ac" );
        var_desc_test_find( lst, "xyz$ac2" );

        release_var_desc_list( lst );
    }
}

/* ============================================================================================================= */
/* private: the format-elements = literal, String, uin64_t */
typedef struct var_fmt_entry_t {
    var_fmt_type_t type;    /* which one of the options is it? */
    const String * literal; /* this one is owned by this struct! */
    uint8_t idx;            /* which str/int-arg to use here */
    uint8_t idx2;           /* the alternative idx, for vft_str and client gives a NULL */
} var_fmt_entry_t;
/* ============================================================================================================= */


/* create a literal format-element, makes a copy of the given string and owns that copy */
static var_fmt_entry_t * var_fmt_entry_create_literal( const char * src, size_t len ) {
    if ( 0 == len || NULL == src ) {
        return NULL;
    }
    else {
        var_fmt_entry_t * res = calloc( 1, sizeof * res );
        if ( NULL != res ) {
            rc_t rc;
            String S;
            StringInit( &S, src, len, len );
            rc = StringCopy( &( res -> literal ), &S );
            if ( 0 == rc ) {
                res -> type = vft_literal;
            } else {
                free( ( void * ) res );
                res = NULL;
            }
        }
        return res;
    }
}

/* create a string format-element, stores the index of the string to use */
static var_fmt_entry_t * var_fmt_entry_create( var_fmt_type_t t, uint8_t idx, uint8_t idx2 ) {
    var_fmt_entry_t * res = calloc( 1, sizeof * res );
    if ( NULL != res ) {
        res -> type = t;
        res -> idx = idx;
        res -> idx2 = idx2;
    }
    return res;
}

static var_fmt_entry_t * clone_var_fmt_entry( const var_fmt_entry_t * src ) {
    var_fmt_entry_t * res = NULL;
    if ( NULL != src ) {
        res = var_fmt_entry_create( src -> type, src -> idx, src -> idx2 );
        if ( NULL != res ) {
            if ( vft_literal == res -> type ) {
                rc_t rc = StringCopy( &( res -> literal ), src -> literal );
                if ( 0 != rc ) {
                    free( ( void * ) res );
                    res = NULL;
                }
            }
        }
    }
    return res;
}

static void var_fmt_String_to_buffer( SBuffer_t *buffer, const String * src ) {
    if ( NULL != src && NULL != buffer ) {
        uint32_t i = 0;
        String * dst = &( buffer -> S );
        char * dst_addr = ( char * )( dst -> addr );
        while ( i < src -> len && dst -> len < buffer -> buffer_size ) {
            dst_addr [ dst -> len ] = src -> addr[ i++ ];
            dst -> len += 1;
        }
    }
}

static void var_fmt_entry_int_value_to_buffer( SBuffer_t *buffer, uint64_t value ) {
    char temp[ 21 ];
    size_t temp_written;
    rc_t rc = string_printf ( temp, sizeof temp, &temp_written, "%lu", value );
    if ( 0 == rc ) {
        String S;
        StringInitCString( &S, temp );
        var_fmt_String_to_buffer( buffer, &S );
    }
}

static void var_fmt_entry_int_to_buffer( const var_fmt_entry_t * self, SBuffer_t *buffer,
                                         const uint64_t * args, size_t args_len ) {
    if ( NULL != args && self -> idx < args_len ) {
        var_fmt_entry_int_value_to_buffer( buffer, args[ self -> idx ] );
    }
}

/*we need both: str-args AND int-args, because of the alternative idx-usage */
static void var_fmt_entry_str_to_buffer( const var_fmt_entry_t * self, SBuffer_t *buffer,
                                         const String ** str_args, size_t str_args_len,
                                         const uint64_t * int_args, size_t int_args_len ) {
    if ( NULL != str_args && self -> idx < str_args_len ) {
        const String * src = str_args[ self -> idx ];
        if ( NULL != src ) {
            /* we have a string to use */
            if ( self -> idx2 == 0xFF ) {
                /* there is no alternative -> print the string even if len == 0 */
                var_fmt_String_to_buffer( buffer, src );
            } else {
                if ( src -> len > 0 ) {
                    /* there is an alternative, but we have a valid string to print */
                    var_fmt_String_to_buffer( buffer, src );
                } else if ( NULL != int_args && self -> idx2 < int_args_len ) {
                    /* there is an alternative - and we have an empty string -  */
                    var_fmt_entry_int_value_to_buffer( buffer, int_args[ self -> idx2 ] );
                }
            }
        } else if ( NULL != int_args && self -> idx2 < int_args_len ) {
            /* the string-source is NULL, and we have an alternative to use */
            var_fmt_entry_int_value_to_buffer( buffer, int_args[ self -> idx2 ] );
        }
    } else if ( NULL != int_args && self -> idx2 < int_args_len ) {
        /* there is not even a array of strings, and we have an alternative to use */
        var_fmt_entry_int_value_to_buffer( buffer, int_args[ self -> idx2 ] );
    }
}
    
/* releases an element, data-pointer to match VectorWhack-callback */
static void destroy_var_fmt_entry( void * self, void * data ) {
    if ( NULL != self ) {
        var_fmt_entry_t * vft = ( var_fmt_entry_t * )self;
        if ( vft_literal == vft -> type ) { StringWhack ( vft -> literal ); }
        free( self );
    }
}

/* ============================================================================================================= */
typedef struct var_fmt_t {
    Vector elements;        /* the elements are pointers to var_fmt_entry_t - structs */
    size_t fixed_len;       /* sum of all literal elements + sum of dflt-len of int-elements */
    SBuffer_t buffer;       /* internal buffer to print into */
} var_fmt_t;
/* ============================================================================================================= */

static void var_fmt_add_entry( var_fmt_t * self, var_fmt_entry_t * entry ) {
    if ( NULL == self || NULL == entry ) { return; }
    rc_t rc = VectorAppend ( &( self -> elements ), NULL, entry );
    if ( 0 != rc ) {
        destroy_var_fmt_entry( entry, NULL );
    }
}

static bool var_fmt_find_and_add( var_fmt_t * self,
                                  const String * T,
                                  const struct var_desc_list_t * vars ) {
    // do we have so far a match against any variable-name at the end of T ?
    const var_desc_t * found = var_desc_list_find( vars, T );
    if ( NULL != found ) {
        /* yes it does! - the key is at the end of T
           T -> len cannot be shorter than item -> name -> len
           because otherwise the key would not be found by var_name_find() */
        uint32_t literal_len = ( T -> len ) - ( found -> name -> len ); 
        if ( literal_len > 0 ) {
            var_fmt_add_entry( self, var_fmt_entry_create_literal( T -> addr, literal_len ) );
        }
        /* we always have a non-literal,
           because otherwise the key would not have been found */
        var_fmt_add_entry( self, var_fmt_entry_create( found -> type, found -> idx, found -> idx2 ) );
    }
    return ( NULL != found );
}

static size_t var_fmt_calc_fixed_len( Vector * v ) {
    size_t res = 0;
    uint32_t i, l = VectorLength( v );
    for ( i = VectorStart( v ); i < l; ++i ) {
        const var_fmt_entry_t * entry = VectorGet ( v, i );
        switch( entry -> type ) {
            case vft_literal: res += entry -> literal -> len; break;
            case vft_int    : res += 20; break;     /* the length of max_uint64_t as string */
            case vft_str    : break;    /* we do not know yet... */
        }
    }
    return res;
}

/* create an empty var-print struct, to be added into later */
struct var_fmt_t * create_empty_var_fmt( size_t buffer_size ) {
    var_fmt_t * self = calloc( 1, sizeof * self );
    if ( NULL != self ) {
        VectorInit ( &( self -> elements ), 0, 12 );
        make_SBuffer( &( self -> buffer ), buffer_size );
    }
    return self;
}

void var_fmt_append( struct var_fmt_t * self,  const String * fmt, const struct var_desc_list_t * vars ) {
    if ( NULL != self && NULL != fmt ) {
        uint32_t i;
        String temp = { fmt -> addr, 0, 0 };
        for ( i = 0; i < fmt -> len; ++i ) {
            if ( var_fmt_find_and_add( self, &temp, vars ) ) {
                /* advance T to restart searching... */
                temp . addr += temp . len;
                temp . len = 1;
                temp . size = 1;
            } else {
                temp . len++;
                temp . size++;
            }
        }
        /* handle what is left in T */
        if ( !var_fmt_find_and_add( self, &temp, vars ) ) {
            var_fmt_add_entry( self, var_fmt_entry_create_literal( temp . addr, temp . len ) );
        }
        /* calculate new fixed-len, and adjust print-buffer */
        self -> fixed_len = var_fmt_calc_fixed_len( &( self -> elements ) );
        increase_SBuffer_to( &( self -> buffer ), ( self -> fixed_len * 4 ) );
    }
}

void var_fmt_append_str( struct var_fmt_t * self,  const char * fmt, const struct var_desc_list_t * vars ) {
    if ( NULL != self && NULL != fmt ) {
        String FMT;
        StringInitCString( &FMT, fmt );
        var_fmt_append( self, &FMT, vars );
    }
}

/* create a var-print struct and fill it with var_fmt_t - elements */
struct var_fmt_t * create_var_fmt( const String * fmt, const struct var_desc_list_t * vars ) {
    var_fmt_t * self = create_empty_var_fmt( 2048 );
    if ( NULL != self ) {
        var_fmt_append( self, fmt, vars );
    }
    return self;    
}

struct var_fmt_t * create_var_fmt_str( const char * fmt, const struct var_desc_list_t * vars ) {
    var_fmt_t * self = create_empty_var_fmt( 2048 );
    if ( NULL != self ) {
        var_fmt_append_str( self, fmt, vars );
    }
    return self;    
}

struct var_fmt_t * var_fmt_clone( const struct var_fmt_t * src ) {
    var_fmt_t * self = NULL;
    if ( NULL != src ) {
        self = create_empty_var_fmt( src -> buffer . buffer_size );
        if ( NULL != self ) {
            const Vector * v = &( src -> elements );
            uint32_t i, l = VectorLength( v );
            for ( i = VectorStart( v ); i < l; ++i ) {
                var_fmt_add_entry( self, clone_var_fmt_entry(  VectorGet( v, i ) ) );
            }
            self -> fixed_len = var_fmt_calc_fixed_len( &( self -> elements ) );
            increase_SBuffer_to( &( self -> buffer ), ( self -> fixed_len * 4 ) );
        }
    }
    return self;
}

/* release a var-print struct and call destructors on its elements */
void release_var_fmt( struct var_fmt_t * self ) {
    if ( NULL != self ) {
        VectorWhack ( &( self -> elements ), destroy_var_fmt_entry, NULL );
        release_SBuffer( &( self -> buffer ) );
        free( ( void * ) self );
    }
}

size_t var_fmt_buffer_size( const struct var_fmt_t * self,
                    const String ** str_args, size_t str_args_len ) {
    size_t res = 0;
    if ( NULL != self ) {
        const Vector * v = &( self -> elements );
        uint32_t i, l = VectorLength( v );
        res = self -> fixed_len;
        for ( i = VectorStart( v ); i < l; ++i ) {
            const var_fmt_entry_t * entry = VectorGet( v, i );
            if ( NULL != entry ) {
                if ( vft_str == entry -> type ) {
                    if ( entry -> idx < str_args_len ) {
                        const String * S = str_args[ entry -> idx ];
                        if ( NULL != S ) {
                            res += S -> len;
                        }
                    }
                }
            }
        }
    }
    return res;
}

void var_fmt_debug( const struct var_fmt_t * self ) {
    if ( NULL != self ) {
        const Vector * v = &( self -> elements );
        uint32_t i, l = VectorLength( v );
        KOutMsg( "\nvar-fmt:" );
        for ( i = VectorStart( v ); i < l; ++i ) {
            const var_fmt_entry_t * entry = VectorGet( v, i );
            if ( NULL != entry ) {
                switch ( entry -> type ) {
                    case vft_literal : KOutMsg( "\nliteral: '%S'", entry -> literal  ); break;
                    case vft_str     : KOutMsg( "\nstr: #%u", entry -> idx  ); break;
                    case vft_int     : KOutMsg( "\nint: #%u", entry -> idx  ); break;
                    default          : KOutMsg( "\nunknown entry-type" ); break;
                }
            }
        }
        KOutMsg( "\ndone\n" );
    }
}

/* apply the var-fmt-struct to the given arguments, write result to buffer */
SBuffer_t * var_fmt_to_buffer( struct var_fmt_t * self,
                    const String ** str_args, size_t str_args_len,
                    const uint64_t * int_args, size_t int_args_len ) {
    SBuffer_t * res = NULL;
    if ( NULL != self )
    {
        size_t needed = var_fmt_buffer_size( self, str_args, str_args_len );
        if ( needed > 0 )
        {
            rc_t rc = increase_SBuffer_to( &( self -> buffer ), needed );
            if ( 0 == rc )
            {
                const Vector * v = &( self -> elements );
                uint32_t i, l = VectorLength( v );
                SBuffer_t * dst = &( self -> buffer );
                self -> buffer . S . len = 0;
                self -> buffer . S . size = 0;

                for ( i = VectorStart( v ); i < l; ++i ) {
                    const var_fmt_entry_t * entry = VectorGet( v, i );
                    if ( NULL != entry ) {
                        switch ( entry -> type ) {
                            /* we enter a string literal */
                            case vft_literal : var_fmt_String_to_buffer( dst,
                                                                         entry -> literal );
                                               break;

                            /* we enter a string argument ( supply the int-vector too, because of the alternative! ) */
                            case vft_str    : var_fmt_entry_str_to_buffer( entry, dst,
                                                                           str_args, str_args_len,
                                                                           int_args, int_args_len );
                                              break;

                            /* we enter a int argument */
                            case vft_int    : var_fmt_entry_int_to_buffer( entry, dst, int_args, int_args_len ); break;
                        }
                    }
                }
                self -> buffer . S . size = self -> buffer . S . len;
                res = &( self -> buffer );
            }
        }
    }
    return res;
}

/* apply the var-fmt-struct to the given arguments, print the result to stdout */
rc_t var_fmt_to_stdout( struct var_fmt_t * self,
                    const String ** str_args, size_t str_args_len,
                    const uint64_t * int_args, size_t int_args_len ) {
    rc_t rc;
    SBuffer_t * t = var_fmt_to_buffer( self, str_args, str_args_len, int_args, int_args_len );
    if ( NULL != t ) {
        rc = KOutMsg( "%S", &( t -> S ) );
    } else {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    }
    return rc;
}

/* apply the var-fmt-struct to the given arguments, print the result to file */
rc_t var_fmt_to_file( struct var_fmt_t * self,
                    KFile * f,
                    uint64_t * pos,
                    const String ** str_args, size_t str_args_len,
                    const uint64_t * int_args, size_t int_args_len ) {
    rc_t rc;
    if ( NULL == self || NULL == f || NULL == pos ) {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    } else {
        SBuffer_t * t = var_fmt_to_buffer( self, str_args, str_args_len, int_args, int_args_len );
        if ( NULL != t ) {
            size_t num_writ;
            rc = KFileWrite( f, *pos, t -> S . addr, t -> S . len, &num_writ );
            if ( 0 == rc ) {
                *pos += num_writ;
            }
        } else {
            rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
        }
    }
    return rc;
}

void var_fmt_test( void ) {
    var_fmt_t * fmt = NULL;
    var_desc_list_t * desc_lst = NULL;
    
    KOutMsg( "var-fmt-test\n" );

    desc_lst = create_var_desc_list();
    if ( NULL != desc_lst ) {
        var_desc_list_add_str( desc_lst, "$ac", 0, 0xFF );
        var_desc_list_add_str( desc_lst, "$sg", 1, 0xFF );
        var_desc_list_add_int( desc_lst, "$si", 0 );
        var_desc_list_add_int( desc_lst, "$sl", 1 );

        fmt = create_var_fmt_str( ">$ac.$si/$sl this $ac/$sg is a test $si-$sl format", desc_lst );
        release_var_desc_list( desc_lst );
    }

    if ( NULL != fmt ) {
        const char * s_acc = "SRR1234567";
        const char * s_grp = "SG_1";
        String S_acc;
        String S_grp;
        const String * strings[ 2 ];
        uint64_t ints[ 2 ];

        var_fmt_debug( fmt );

        StringInitCString( &S_acc, s_acc );
        StringInitCString( &S_grp, s_grp );
        strings[ 0 ] = &S_acc;
        strings[ 1 ] = &S_grp;
        ints[ 0 ] = 1001;
        ints[ 1 ] = 77;

        {
            size_t needed = var_fmt_buffer_size( fmt, strings, 2 );
            KOutMsg( "recomended buffer-size = %u\n", needed );
        }

        {
            var_fmt_t * cloned = var_fmt_clone( fmt );
            var_fmt_append_str( cloned, " and this!\n", NULL );
            var_fmt_to_stdout( cloned, strings, 2, ints, 2 );
        }
    }
    release_var_fmt( fmt );
}
