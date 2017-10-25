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

#include "cmn_iter.h"
#include "helper.h"

#include <klib/out.h>
#include <sra/sraschema.h>

#include <vdb/manager.h>
#include <vdb/schema.h>
#include <vdb/table.h>
#include <vdb/cursor.h>
#include <vdb/database.h>

#include <os-native.h>
#include <sysalloc.h>

typedef struct cmn_iter
{
    const VDBManager * mgr;
    VSchema * schema;
    const VDatabase * db;
    const VTable * tbl;
    const VCursor * cursor;
    const char * row_range;
    struct num_gen * ranges;
    const struct num_gen_iter * row_iter;
    uint64_t row_count;
    int64_t first_row, row_id;
} cmn_iter;


void destroy_cmn_iter( struct cmn_iter * iter )
{
    if ( iter != NULL )
    {
        if ( iter -> row_iter != NULL )
            num_gen_iterator_destroy( iter -> row_iter );
        if ( iter -> ranges != NULL )
            num_gen_destroy( iter -> ranges );
        if ( iter -> cursor != NULL )
            VCursorRelease( iter -> cursor );
        if ( iter -> tbl != NULL )
            VTableRelease( iter -> tbl );
        if ( iter -> db != NULL )
            VDatabaseRelease( iter -> db );
        if ( iter -> schema != NULL )
            VSchemaRelease( iter -> schema );
        if ( iter -> mgr != NULL )
            VDBManagerRelease( iter -> mgr );
        free( ( void * ) iter );
    }
}


rc_t make_cmn_iter( const cmn_params * cp, const char * tblname, struct cmn_iter ** iter )
{
    rc_t rc = 0;
    cmn_iter * i = calloc( 1, sizeof * i );
    if ( i == NULL )
    {
        rc = RC( rcVDB, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
        ErrMsg( "make_cmn_iter.calloc( %d ) -> %R", ( sizeof * i ), rc );
    }
    else
    {
        rc = VDBManagerMakeRead( &i -> mgr, cp -> dir );
        if ( rc != 0 )
            ErrMsg( "make_cmn_iter.VDBManagerMakeRead() -> %R\n", rc );
        else
        {
            rc = VDBManagerMakeSRASchema( i -> mgr, &( i -> schema ) );
            if ( rc != 0 )
                ErrMsg( "make_cmn_iter.VDBManagerMakeSRASchema() -> %R\n", rc );
            else
            {
                rc = VDBManagerOpenDBRead( i -> mgr, &( i -> db ), i -> schema, "%s", cp -> accession );
                if ( rc != 0 )
                    ErrMsg( "make_cmn_iter.VDBManagerOpenDBRead( '%s' ) -> %R\n", cp -> accession, rc );
                else
                {
                    rc = VDatabaseOpenTableRead( i -> db, &( i -> tbl ), "%s", tblname );
                    if ( rc != 0 )
                        ErrMsg( "make_cmn_iter.VDBManagerOpenDBRead( '%s', '%s' ) -> %R\n", cp -> accession, tblname, rc );
                    else
                    {
                        rc = VTableCreateCachedCursorRead( i -> tbl, &( i -> cursor ), cp -> cursor_cache );
                        if ( rc != 0 )
                            ErrMsg( "make_cmn_iter.VTableCreateCachedCursorRead() -> %R\n", rc );
                        else
                        {
                            i -> row_range = NULL;
                            i -> first_row = cp -> first_row;
                            i -> row_count = cp -> row_count;
                            
                            *iter = i;
                        }
                    }
                }
            }
        }
    }
    if ( rc != 0 )
        destroy_cmn_iter( i );
    return rc;
}


rc_t cmn_iter_add_column( struct cmn_iter * iter, const char * name, uint32_t * id )
{
    return add_column( iter -> cursor, name, id );
}


int64_t cmn_iter_row_id( const struct cmn_iter * iter )
{
    return iter -> row_id;
}


uint64_t cmn_iter_row_count( struct cmn_iter * iter )
{
    uint64_t res = 0;
    rc_t rc = num_gen_iterator_count( iter -> row_iter, &res );
    if ( rc != 0 )
        ErrMsg( "make_cmn_iter.num_gen_iterator_count() -> %R\n", rc );
    return res;
}


bool cmn_iter_next( struct cmn_iter * iter, rc_t * rc )
{
    return num_gen_iterator_next( iter -> row_iter, &iter -> row_id, rc );
}


rc_t cmn_iter_range( struct cmn_iter * iter, uint32_t col_id )
{
    rc_t rc = VCursorOpen( iter -> cursor );
    if ( rc != 0 )
        ErrMsg( "cmn_iter_range.VCursorOpen() -> %R", rc );
    else
    {
        rc = num_gen_make_sorted( &iter -> ranges, true );
        if ( rc != 0 )
            ErrMsg( "cmn_iter_range.num_gen_make_sorted() -> %R\n", rc );
        else
        {
            if ( iter -> row_range != NULL )
            {
                rc = num_gen_parse( iter -> ranges, iter -> row_range );
                if ( rc != 0 )
                    ErrMsg( "cmn_iter_range.num_gen_parse( %s ) -> %R\n", iter->row_range, rc );
            }
            else if ( iter -> row_count > 0 )
            {
                rc = num_gen_add( iter -> ranges, iter -> first_row, iter -> row_count );
                if ( rc != 0 )
                    ErrMsg( "cmn_iter_range.num_gen_add( %ld.%lu ) -> %R\n",
                            iter -> first_row, iter -> row_count, iter -> row_range, rc );
            }
        }
    }

    if ( rc == 0 )
    {
        rc = VCursorIdRange( iter -> cursor, col_id, &iter -> first_row, &iter -> row_count );
        if ( rc != 0 )
            ErrMsg( "cmn_iter_range.VCursorIdRange() -> %R", rc );
        else
        {
            rc = make_row_iter( iter -> ranges, iter -> first_row, iter -> row_count, &iter -> row_iter );
            if ( rc != 0 )
                ErrMsg( "cmn_iter_range.make_row_iter( %s ) -> %R\n", iter -> row_range, rc );
        }
    }
    return rc;
}


rc_t cmn_iter_copy_range( struct cmn_iter * self, const struct cmn_iter * src )
{
    rc_t rc;
    self -> first_row = src -> first_row;
    self -> row_count = src -> row_count;
    if ( self -> row_iter != NULL )
        num_gen_iterator_destroy( self -> row_iter );
    rc = make_row_iter( self -> ranges, self -> first_row, self -> row_count, &self -> row_iter );
    if ( rc != 0 )
        ErrMsg( "cmn_iter_range.make_row_iter( %s ) -> %R\n", self -> row_range, rc );
    return rc;
}

rc_t cmn_read_uint64( struct cmn_iter * iter, uint32_t col_id, uint64_t *value )
{
    uint32_t elem_bits, boff, row_len;
    const uint64_t * value_ptr;
    rc_t rc = VCursorCellDataDirect( iter -> cursor, iter -> row_id, col_id, &elem_bits,
                                 (const void **)&value_ptr, &boff, &row_len );
    if ( rc != 0 )
        ErrMsg( "VCursorCellDataDirect( #%ld ) -> %R\n", iter->row_id, rc );
    else if ( elem_bits != 64 || boff != 0 || row_len < 1 )
    {
        ErrMsg( "row#%ld : bits=%d, boff=%d, len=%d\n", iter -> row_id, elem_bits, boff, row_len );
        rc = RC( rcApp, rcNoTarg, rcAccessing, rcRow, rcInvalid );
    }
    else
        *value = *value_ptr;
    return rc;
}


rc_t cmn_read_uint64_array( struct cmn_iter * iter, uint32_t col_id, uint64_t *value,
                            uint32_t num_values, uint32_t * values_read )
{
    uint32_t elem_bits, boff, row_len;
    const uint64_t * value_ptr;
    rc_t rc = VCursorCellDataDirect( iter -> cursor, iter -> row_id, col_id, &elem_bits,
                                 (const void **)&value_ptr, &boff, &row_len );
    if ( rc != 0 )
        ErrMsg( "VCursorCellDataDirect( #%ld ) -> %R\n", iter->row_id, rc );
    else if ( elem_bits != 64 || boff != 0 || row_len < 1 )
    {
        ErrMsg( "row#%ld : bits=%d, boff=%d, len=%d\n", iter -> row_id, elem_bits, boff, row_len );
        rc = RC( rcApp, rcNoTarg, rcAccessing, rcRow, rcInvalid );
    }
    else
    {
        if ( row_len > num_values ) row_len = num_values;
        if ( values_read != NULL )
            * values_read = row_len;
        memmove( (void *)value, (void *)value_ptr, row_len * 8 );
    }
    return rc;
}


rc_t cmn_read_uint32( struct cmn_iter * iter, uint32_t col_id, uint32_t *value )
{
    uint32_t elem_bits, boff, row_len;
    const uint32_t * value_ptr;
    rc_t rc = VCursorCellDataDirect( iter -> cursor, iter -> row_id, col_id, &elem_bits,
                                 (const void **)&value_ptr, &boff, &row_len );
    if ( rc != 0 )
        ErrMsg( "VCursorCellDataDirect( #%ld ) -> %R\n", iter -> row_id, rc );
    else if ( elem_bits != 32 || boff != 0 || row_len < 1 )
    {
        ErrMsg( "row#%ld : bits=%d, boff=%d, len=%d\n", iter -> row_id, elem_bits, boff, row_len );
        rc = RC( rcApp, rcNoTarg, rcAccessing, rcRow, rcInvalid );
    }
    else
        *value = *value_ptr;
    return rc;
}

rc_t cmn_read_uint32_array( struct cmn_iter * iter, uint32_t col_id, uint32_t *value,
                            uint32_t num_values, uint32_t * values_read )
{
    uint32_t elem_bits, boff, row_len;
    const uint32_t * value_ptr;
    rc_t rc = VCursorCellDataDirect( iter -> cursor, iter -> row_id, col_id, &elem_bits,
                                 (const void **)&value_ptr, &boff, &row_len );
    if ( rc != 0 )
        ErrMsg( "VCursorCellDataDirect( #%ld ) -> %R\n", iter -> row_id, rc );
    else if ( elem_bits != 32 || boff != 0 || row_len < 1 )
    {
        ErrMsg( "row#%ld : bits=%d, boff=%d, len=%d\n", iter -> row_id, elem_bits, boff, row_len );
        rc = RC( rcApp, rcNoTarg, rcAccessing, rcRow, rcInvalid );
    }
    else
    {
        if ( row_len > num_values ) row_len = num_values;
        if ( values_read != NULL )
            * values_read = row_len;
        memmove( (void *)value, (void *)value_ptr, row_len * 4 );
    }
    return rc;
}

rc_t cmn_read_String( struct cmn_iter * iter, uint32_t col_id, String *value )
{
    uint32_t elem_bits, boff;
    rc_t rc = VCursorCellDataDirect( iter -> cursor, iter -> row_id, col_id, &elem_bits,
                                 (const void **)&value->addr, &boff, &value -> len );
    if ( rc != 0 )
        ErrMsg( "VCursorCellDataDirect( #%ld ) -> %R\n", iter -> row_id, rc );
    else if ( elem_bits != 8 || boff != 0 )
    {
        ErrMsg( "row#%ld : bits=%d, boff=%d, len=%d\n", iter -> row_id, elem_bits, boff, value -> len );
        rc = RC( rcApp, rcNoTarg, rcAccessing, rcRow, rcInvalid );
    }
    else
        value -> size = value -> len;
    return rc;
}
