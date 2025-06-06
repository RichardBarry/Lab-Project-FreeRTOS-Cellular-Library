/*
 * FreeRTOS-Cellular-Interface <DEVELOPMENT BRANCH>
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 */

/**
 * @brief FreeRTOS Cellular Library common AT commnad library.
 */

#ifndef CELLULAR_DO_NOT_USE_CUSTOM_CONFIG
    /* Include custom config file before other headers. */
    #include "cellular_config.h"
#endif
#include "cellular_config_defaults.h"

/* Standard includes. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "cellular_platform.h"
#include "cellular_types.h"
#include "cellular_internal.h"
#include "cellular_common_internal.h"
#include "cellular_pkthandler_internal.h"
#include "cellular_pktio_internal.h"
#include "cellular_at_core.h"

/*-----------------------------------------------------------*/

#define SIGNAL_QUALITY_CSQ_UNKNOWN      ( 99 )
#define SIGNAL_QUALITY_CSQ_RSSI_MIN     ( 0 )
#define SIGNAL_QUALITY_CSQ_RSSI_MAX     ( 31 )
#define SIGNAL_QUALITY_CSQ_BER_MIN      ( 0 )
#define SIGNAL_QUALITY_CSQ_BER_MAX      ( 7 )
#define SIGNAL_QUALITY_CSQ_RSSI_BASE    ( -113 )
#define SIGNAL_QUALITY_CSQ_RSSI_STEP    ( 2 )

/* Only supports a single cellular instance. */
#define CELLULAR_CONTEXT_MAX            ( 1U )

/*-----------------------------------------------------------*/

/**
 * @ingroup cellular_datatypes_structs
 * @brief Parameters in Signal Bars Look up table for measuring Signal Bars.
 */
typedef struct signalBarsTable
{
    int8_t upperThreshold; /**<  Threshold for signal bars. */
    uint8_t bars;          /**<  The level in integer of bars. */
} signalBarsTable_t;

/*-----------------------------------------------------------*/

static CellularContext_t * _Cellular_AllocContext( void );
static void _Cellular_FreeContext( CellularContext_t * pContext );
static void _shutdownCallback( CellularContext_t * pContext );
static CellularError_t libOpen( CellularContext_t * pContext );
static void libClose( CellularContext_t * pContext );
static bool _Cellular_CreateLibStatusMutex( CellularContext_t * pContext );
static void _Cellular_DestroyLibStatusMutex( CellularContext_t * pContext );
/* Internal function of _Cellular_CreateSocket to reduce complexity. */
static void createSocketSetSocketData( uint8_t contextId,
                                       uint8_t socketId,
                                       CellularSocketDomain_t socketDomain,
                                       CellularSocketType_t socketType,
                                       CellularSocketProtocol_t socketProtocol,
                                       CellularSocketContext_t * pSocketData );
static uint8_t _getSignalBars( int16_t compareValue,
                               CellularRat_t rat );
static CellularError_t checkInitParameter( const CellularHandle_t * pCellularHandle,
                                           const CellularCommInterface_t * pCommInterface,
                                           const CellularTokenTable_t * pTokenTable );

static void _Cellular_SetShutdownCallback( CellularContext_t * pContext,
                                           _pPktioShutdownCallback_t shutdownCb );

/*-----------------------------------------------------------*/

#if ( CELLULAR_CONFIG_STATIC_ALLOCATION_CONTEXT == 1 )
static CellularContext_t cellularStaticContextTable[ CELLULAR_CONTEXT_MAX ] = { 0 };
#endif

static CellularContext_t * cellularContextTable[ CELLULAR_CONTEXT_MAX ] = { 0 };

#if ( CELLULAR_CONFIG_STATIC_SOCKET_CONTEXT_ALLOCATION == 1 )
static CellularSocketContext_t cellularStaticSocketDataTable[ CELLULAR_NUM_SOCKET_MAX ] = { 0 };
#endif

/*-----------------------------------------------------------*/

static CellularContext_t * _Cellular_AllocContext( void )
{
    CellularContext_t * pContext = NULL;
    uint8_t i = 0;

    for( i = 0; i < CELLULAR_CONTEXT_MAX; i++ )
    {
        if( cellularContextTable[ i ] == NULL )
        {
            #if ( CELLULAR_CONFIG_STATIC_ALLOCATION_CONTEXT == 1 )
            {
                pContext = &( cellularStaticContextTable[ i ] );
            }
            #else
            {
                pContext = ( CellularContext_t * ) Platform_Malloc( sizeof( CellularContext_t ) );
            }
            #endif

            if( pContext != NULL )
            {
                ( void ) memset( pContext, 0, sizeof( CellularContext_t ) );
                cellularContextTable[ i ] = pContext;
            }

            break;
        }
    }

    return pContext;
}

/*-----------------------------------------------------------*/

static void _Cellular_FreeContext( CellularContext_t * pContext )
{
    uint8_t i = 0;

    for( i = 0; i < CELLULAR_CONTEXT_MAX; i++ )
    {
        if( cellularContextTable[ i ] == pContext )
        {
            cellularContextTable[ i ] = NULL;
            #if ( CELLULAR_CONFIG_STATIC_ALLOCATION_CONTEXT == 0 )
            {
                Platform_Free( pContext );
            }
            #endif
            break;
        }
    }
}

/*-----------------------------------------------------------*/

static void _shutdownCallback( CellularContext_t * pContext )
{
    pContext->bLibShutdown = true;
}

/*-----------------------------------------------------------*/

static CellularError_t libOpen( CellularContext_t * pContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularPktStatus_t pktStatus = CELLULAR_PKT_STATUS_OK;

    CELLULAR_CONFIG_ASSERT( pContext != NULL );

    PlatformMutex_Lock( &( pContext->libStatusMutex ) );

    ( CellularPktStatus_t ) _Cellular_AtParseInit( pContext );
    _Cellular_LockAtDataMutex( pContext );
    _Cellular_InitAtData( pContext, 0 );
    _Cellular_UnlockAtDataMutex( pContext );
    _Cellular_SetShutdownCallback( pContext, &_shutdownCallback );
    pktStatus = _Cellular_PktHandlerInit( pContext );

    if( pktStatus == CELLULAR_PKT_STATUS_OK )
    {
        pktStatus = _Cellular_PktioInit( pContext, &_Cellular_HandlePacket );

        if( pktStatus != CELLULAR_PKT_STATUS_OK )
        {
            LogError( ( "pktio failed to initialize" ) );
            _Cellular_PktioShutdown( pContext );
            _Cellular_PktHandlerCleanup( pContext );
        }
    }

    if( pktStatus != CELLULAR_PKT_STATUS_OK )
    {
        cellularStatus = _Cellular_TranslatePktStatus( pktStatus );
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        /* CellularLib open finished. */
        pContext->bLibOpened = true;
        pContext->bLibShutdown = false;
    }

    PlatformMutex_Unlock( &( pContext->libStatusMutex ) );

    return cellularStatus;
}

/*-----------------------------------------------------------*/

static void libClose( CellularContext_t * pContext )
{
    bool bOpened = false;
    uint8_t i = 0;

    CELLULAR_CONFIG_ASSERT( pContext != NULL );

    PlatformMutex_Lock( &( pContext->libStatusMutex ) );
    bOpened = pContext->bLibOpened;

    /* Indicate that CellularLib is in the process of closing. */
    pContext->bLibClosing = true;
    PlatformMutex_Unlock( &( pContext->libStatusMutex ) );

    if( bOpened == true )
    {
        /* Shut down the utilities. */
        _Cellular_PktioShutdown( pContext );
        _Cellular_PktHandlerCleanup( pContext );
    }

    PlatformMutex_Lock( &( pContext->libStatusMutex ) );
    pContext->bLibShutdown = false;
    pContext->bLibOpened = false;
    pContext->bLibClosing = false;

    /* Remove all created sockets. */
    for( i = 0; i < CELLULAR_NUM_SOCKET_MAX; i++ )
    {
        if( pContext->pSocketData[ i ] != NULL )
        {
            #if ( CELLULAR_CONFIG_STATIC_SOCKET_CONTEXT_ALLOCATION == 0 )
            {
                Platform_Free( pContext->pSocketData[ i ] );
            }
            #endif
            pContext->pSocketData[ i ] = NULL;
        }
    }

    PlatformMutex_Unlock( &( pContext->libStatusMutex ) );
    LogDebug( ( "CELLULARLib closed" ) );
}

/*-----------------------------------------------------------*/

static bool _Cellular_CreateLibStatusMutex( CellularContext_t * pContext )
{
    bool status = false;

    status = PlatformMutex_Create( &( pContext->libStatusMutex ), false );

    return status;
}

/*-----------------------------------------------------------*/

static void _Cellular_DestroyLibStatusMutex( CellularContext_t * pContext )
{
    PlatformMutex_Destroy( &( pContext->libStatusMutex ) );
}

/*-----------------------------------------------------------*/

/* Internal function of _Cellular_CreateSocket to reduce complexity. */
static void createSocketSetSocketData( uint8_t contextId,
                                       uint8_t socketId,
                                       CellularSocketDomain_t socketDomain,
                                       CellularSocketType_t socketType,
                                       CellularSocketProtocol_t socketProtocol,
                                       CellularSocketContext_t * pSocketData )
{
    ( void ) memset( pSocketData, 0, sizeof( CellularSocketContext_t ) );
    pSocketData->socketState = SOCKETSTATE_ALLOCATED;
    pSocketData->socketId = socketId;
    pSocketData->contextId = contextId;
    pSocketData->socketDomain = socketDomain;
    pSocketData->socketType = socketType;
    pSocketData->socketProtocol = socketProtocol;
}

/*-----------------------------------------------------------*/

static uint8_t _getSignalBars( int16_t compareValue,
                               CellularRat_t rat )
{
    uint8_t i = 0, tableSize = 0, barsValue = CELLULAR_INVALID_SIGNAL_BAR_VALUE;
    const signalBarsTable_t * pSignalBarsTable = NULL;


    /* Look up Table for Mapping Signal Bars with RSSI value in dBm of GSM Signal.
     * The upper RSSI threshold is maintained in decreasing order to return the signal Bars. */
    static const signalBarsTable_t gsmSignalBarsTable[] =
    {
        { -104, 1 },
        { -98,  2 },
        { -89,  3 },
        { -80,  4 },
        { 0,    5 },
    };

    /* Look up Table for Mapping Signal Bars with RSRP value in dBm of LTE CAT M1 Signal.
     * The upper RSRP threshold is maintained in decreasing order to return the signal Bars. */
    static const signalBarsTable_t lteCATMSignalBarsTable[] =
    {
        { -115, 1 },
        { -105, 2 },
        { -95,  3 },
        { -85,  4 },
        { 0,    5 },
    };

    /* Look up Table for Mapping Signal Bars with RSRP value in dBm of LTE CAT NB1 Signal.
     * The upper RSRP threshold is maintained in decreasing order to return the signal Bars. */
    static const signalBarsTable_t lteNBIotSignalBarsTable[] =
    {
        { -115, 1 },
        { -105, 2 },
        { -95,  3 },
        { -85,  4 },
        { 0,    5 },
    };

    if( ( rat == CELLULAR_RAT_GSM ) || ( rat == CELLULAR_RAT_EDGE ) )
    {
        pSignalBarsTable = gsmSignalBarsTable;
        tableSize = ( uint8_t ) ARRAY_SIZE( gsmSignalBarsTable );
    }

    if( ( rat == CELLULAR_RAT_CATM1 ) || ( rat == CELLULAR_RAT_LTE ) )
    {
        pSignalBarsTable = lteCATMSignalBarsTable;
        tableSize = ( uint8_t ) ARRAY_SIZE( lteCATMSignalBarsTable );
    }

    if( rat == CELLULAR_RAT_NBIOT )
    {
        pSignalBarsTable = lteNBIotSignalBarsTable;
        tableSize = ( uint8_t ) ARRAY_SIZE( lteNBIotSignalBarsTable );
    }

    for( i = 0; i < tableSize; i++ )
    {
        if( compareValue <= pSignalBarsTable[ i ].upperThreshold )
        {
            barsValue = pSignalBarsTable[ i ].bars;
            break;
        }
    }

    return barsValue;
}

/*-----------------------------------------------------------*/

static CellularError_t checkInitParameter( const CellularHandle_t * pCellularHandle,
                                           const CellularCommInterface_t * pCommInterface,
                                           const CellularTokenTable_t * pTokenTable )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pCellularHandle == NULL )
    {
        LogError( ( "Invalid CellularHandler pointer." ) );
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else if( ( pCommInterface == NULL ) || ( pCommInterface->open == NULL ) ||
             ( pCommInterface->close == NULL ) || ( pCommInterface->send == NULL ) ||
             ( pCommInterface->recv == NULL ) )
    {
        LogError( ( "All the functions in the CellularCommInterface should be valid." ) );
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }
    else if( ( pTokenTable == NULL ) || ( pTokenTable->pCellularUrcHandlerTable == NULL ) ||
             ( pTokenTable->pCellularSrcTokenErrorTable == NULL ) ||
             ( pTokenTable->pCellularSrcTokenSuccessTable == NULL ) ||
             ( pTokenTable->pCellularUrcTokenWoPrefixTable == NULL ) )
    {
        LogError( ( "All the token tables in the CellularTokenTable should be valid." ) );
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }
    else
    {
        /* Empty Else MISRA 15.7 */
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

static void _Cellular_SetShutdownCallback( CellularContext_t * pContext,
                                           _pPktioShutdownCallback_t shutdownCb )
{
    pContext->pPktioShutdownCB = shutdownCb;
}

/*-----------------------------------------------------------*/

/* Checks whether Cellular Library is opened. */
CellularError_t _Cellular_CheckLibraryStatus( CellularContext_t * pContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else
    {
        PlatformMutex_Lock( &( pContext->libStatusMutex ) );

        if( pContext->bLibOpened == false )
        {
            cellularStatus = CELLULAR_LIBRARY_NOT_OPEN;
        }

        PlatformMutex_Unlock( &( pContext->libStatusMutex ) );
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        PlatformMutex_Lock( &( pContext->libStatusMutex ) );

        if( ( pContext->bLibShutdown == true ) || ( pContext->bLibClosing == true ) )
        {
            LogError( ( "Cellular Lib indicated a failure[%d][%d]", pContext->bLibShutdown, pContext->bLibClosing ) );
            cellularStatus = CELLULAR_INTERNAL_FAILURE;
        }

        PlatformMutex_Unlock( &( pContext->libStatusMutex ) );
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_TranslatePktStatus( CellularPktStatus_t status )
{
    CellularError_t cellularStatus = CELLULAR_INTERNAL_FAILURE;

    switch( status )
    {
        case CELLULAR_PKT_STATUS_OK:
            cellularStatus = CELLULAR_SUCCESS;
            break;

        case CELLULAR_PKT_STATUS_TIMED_OUT:
            cellularStatus = CELLULAR_TIMEOUT;
            break;

        case CELLULAR_PKT_STATUS_FAILURE:
        case CELLULAR_PKT_STATUS_BAD_REQUEST:
        case CELLULAR_PKT_STATUS_BAD_RESPONSE:
        case CELLULAR_PKT_STATUS_SIZE_MISMATCH:
        default:
            LogError( ( "_Cellular_TranslatePktStatus: Status %d", status ) );
            cellularStatus = CELLULAR_INTERNAL_FAILURE;
            break;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularPktStatus_t _Cellular_TranslateAtCoreStatus( CellularATError_t status )
{
    CellularPktStatus_t pktStatus;

    switch( status )
    {
        case CELLULAR_AT_SUCCESS:
            pktStatus = CELLULAR_PKT_STATUS_OK;
            break;

        case CELLULAR_AT_BAD_PARAMETER:
            pktStatus = CELLULAR_PKT_STATUS_BAD_PARAM;
            break;

        case CELLULAR_AT_NO_MEMORY:
        case CELLULAR_AT_UNSUPPORTED:
        case CELLULAR_AT_MODEM_ERROR:
        case CELLULAR_AT_ERROR:
        case CELLULAR_AT_UNKNOWN:
        default:
            LogError( ( "_Cellular_TranslateAtCoreStatus: Status %d", status ) );
            pktStatus = CELLULAR_PKT_STATUS_FAILURE;
            break;
    }

    return pktStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_CreateSocketData( CellularContext_t * pContext,
                                            uint8_t contextId,
                                            CellularSocketDomain_t socketDomain,
                                            CellularSocketType_t socketType,
                                            CellularSocketProtocol_t socketProtocol,
                                            CellularSocketHandle_t * pSocketHandle )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularSocketContext_t * pSocketData = NULL;
    uint8_t socketId = 0;

    for( socketId = 0; socketId < CELLULAR_NUM_SOCKET_MAX; socketId++ )
    {
        if( pContext->pSocketData[ socketId ] == NULL )
        {
            #if ( CELLULAR_CONFIG_STATIC_SOCKET_CONTEXT_ALLOCATION == 1 )
            {
                pSocketData = &( cellularStaticSocketDataTable[ socketId ] );
            }
            #else
            {
                pSocketData = ( CellularSocketContext_t * ) Platform_Malloc( sizeof( CellularSocketContext_t ) );
            }
            #endif

            if( pSocketData != NULL )
            {
                createSocketSetSocketData( contextId, socketId, socketDomain,
                                           socketType, socketProtocol, pSocketData );
                pContext->pSocketData[ socketId ] = pSocketData;
                *pSocketHandle = ( CellularSocketHandle_t ) pSocketData;
            }
            else
            {
                cellularStatus = CELLULAR_NO_MEMORY;
            }

            break;
        }
    }

    if( cellularStatus == CELLULAR_NO_MEMORY )
    {
        LogError( ( "_Cellular_CreateSocket, Out of memory" ) );
    }
    else if( socketId >= CELLULAR_NUM_SOCKET_MAX )
    {
        LogError( ( "_Cellular_CreateSocket, No free socket slots are available" ) );
        cellularStatus = CELLULAR_NO_MEMORY;
    }
    else
    {
        /* Empty Else MISRA 15.7 */
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_RemoveSocketData( CellularContext_t * pContext,
                                            CellularSocketHandle_t socketHandle )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    uint8_t socketId;

    if( socketHandle->socketState == SOCKETSTATE_CONNECTING )
    {
        LogWarn( ( "_Cellular_RemoveSocket, socket is connecting state [%u]", ( unsigned int ) socketHandle->socketId ) );
    }

    taskENTER_CRITICAL();

    if( pContext != NULL )
    {
        for( socketId = 0; socketId < CELLULAR_NUM_SOCKET_MAX; socketId++ )
        {
            if( pContext->pSocketData[ socketId ] == socketHandle )
            {
                pContext->pSocketData[ socketId ] = NULL;
                break;
            }
        }

        if( socketId == CELLULAR_NUM_SOCKET_MAX )
        {
            cellularStatus = CELLULAR_BAD_PARAMETER;
        }

        #if ( CELLULAR_CONFIG_STATIC_SOCKET_CONTEXT_ALLOCATION == 0 )
            else
            {
                Platform_Free( socketHandle );
            }
        #endif
    }
    else
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }

    taskEXIT_CRITICAL();

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_IsValidSocket( const CellularContext_t * pContext,
                                         uint32_t sockIndex )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else
    {
        if( ( sockIndex >= CELLULAR_NUM_SOCKET_MAX ) || ( pContext->pSocketData[ sockIndex ] == NULL ) )
        {
            LogError( ( "_Cellular_IsValidSocket, invalid socket handle %u", ( unsigned int ) sockIndex ) );
            cellularStatus = CELLULAR_BAD_PARAMETER;
        }
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_IsValidPdn( uint8_t contextId )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( ( contextId > CELLULAR_PDN_CONTEXT_ID_MAX ) || ( contextId < CELLULAR_PDN_CONTEXT_ID_MIN ) )
    {
        LogError( ( "_Cellular_IsValidPdn: ContextId out of range %d",
                    contextId ) );
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_ConvertCsqSignalRssi( int16_t csqRssi,
                                                int16_t * pRssiValue )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    int16_t rssiValue = 0;

    if( pRssiValue == NULL )
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        if( csqRssi == ( SIGNAL_QUALITY_CSQ_UNKNOWN ) )
        {
            rssiValue = CELLULAR_INVALID_SIGNAL_VALUE;
        }
        else if( ( csqRssi < SIGNAL_QUALITY_CSQ_RSSI_MIN ) || ( csqRssi > SIGNAL_QUALITY_CSQ_RSSI_MAX ) )
        {
            cellularStatus = CELLULAR_BAD_PARAMETER;
        }
        else
        {
            rssiValue = ( int16_t ) ( SIGNAL_QUALITY_CSQ_RSSI_BASE + ( csqRssi * SIGNAL_QUALITY_CSQ_RSSI_STEP ) );
        }
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        *pRssiValue = rssiValue;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_ConvertCsqSignalBer( int16_t csqBer,
                                               int16_t * pBerValue )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    int16_t berValue = 0;

    static const uint16_t rxEqualValueToBerTable[] =
    {
        14,  /* Assumed value 0.14%. */
        28,  /* Assumed value 0.28%.*/
        57,  /* Assumed value 0.57%. */
        113, /* Assumed value 1.13%. */
        226, /* Assumed value 2.26%. */
        453, /* Assumed value 4.53%. */
        905, /* Assumed value 9.05%. */
        1810 /* Assumed value 18.10%. */
    };

    if( pBerValue == NULL )
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        if( csqBer == ( ( int16_t ) SIGNAL_QUALITY_CSQ_UNKNOWN ) )
        {
            berValue = CELLULAR_INVALID_SIGNAL_VALUE;
        }
        else if( ( csqBer < SIGNAL_QUALITY_CSQ_BER_MIN ) || ( csqBer > SIGNAL_QUALITY_CSQ_BER_MAX ) )
        {
            cellularStatus = CELLULAR_BAD_PARAMETER;
        }
        else
        {
            berValue = ( int16_t ) rxEqualValueToBerTable[ csqBer ];
        }
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        *pBerValue = berValue;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_GetModuleContext( const CellularContext_t * pContext,
                                            void ** ppModuleContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else
    {
        *ppModuleContext = pContext->pModuleContext;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_ComputeSignalBars( CellularRat_t rat,
                                             CellularSignalInfo_t * pSignalInfo )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pSignalInfo == NULL )
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }
    else
    {
        if( ( rat == CELLULAR_RAT_GSM ) || ( rat == CELLULAR_RAT_EDGE ) )
        {
            pSignalInfo->bars = _getSignalBars( pSignalInfo->rssi, rat );
            LogDebug( ( "_computeSignalBars: RSSI %d Bars %d", pSignalInfo->rssi, pSignalInfo->bars ) );
        }
        else if( ( rat == CELLULAR_RAT_LTE ) || ( rat == CELLULAR_RAT_CATM1 ) || ( rat == CELLULAR_RAT_NBIOT ) )
        {
            pSignalInfo->bars = _getSignalBars( pSignalInfo->rsrp, rat );
            LogDebug( ( "_computeSignalBars: RSRP %d Bars %d", pSignalInfo->rsrp, pSignalInfo->bars ) );
        }
        else
        {
            cellularStatus = CELLULAR_UNKNOWN;
            pSignalInfo->bars = CELLULAR_INVALID_SIGNAL_BAR_VALUE;
        }
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_GetCurrentRat( CellularContext_t * pContext,
                                         CellularRat_t * pRat )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    cellularStatus = _Cellular_CheckLibraryStatus( pContext );

    if( cellularStatus != CELLULAR_SUCCESS )
    {
        LogDebug( ( "_Cellular_CheckLibraryStatus failed" ) );
    }
    else if( pRat == NULL )
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }
    else
    {
        _Cellular_LockAtDataMutex( pContext );
        *pRat = pContext->libAtData.rat;
        _Cellular_UnlockAtDataMutex( pContext );
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

void _Cellular_NetworkRegistrationCallback( const CellularContext_t * pContext,
                                            CellularUrcEvent_t urcEvent,
                                            const CellularServiceStatus_t * pServiceStatus )
{
    if( ( pContext != NULL ) && ( pContext->cbEvents.networkRegistrationCallback != NULL ) )
    {
        pContext->cbEvents.networkRegistrationCallback( urcEvent, pServiceStatus,
                                                        pContext->cbEvents.pNetworkRegistrationCallbackContext );
    }
}

/*-----------------------------------------------------------*/

void _Cellular_PdnEventCallback( const CellularContext_t * pContext,
                                 CellularUrcEvent_t urcEvent,
                                 uint8_t contextId )
{
    if( ( pContext != NULL ) && ( pContext->cbEvents.pdnEventCallback != NULL ) )
    {
        pContext->cbEvents.pdnEventCallback( urcEvent, contextId, pContext->cbEvents.pPdnEventCallbackContext );
    }
}

/*-----------------------------------------------------------*/

void _Cellular_SignalStrengthChangedCallback( const CellularContext_t * pContext,
                                              CellularUrcEvent_t urcEvent,
                                              const CellularSignalInfo_t * pSignalInfo )
{
    if( ( pContext != NULL ) && ( pContext->cbEvents.signalStrengthChangedCallback != NULL ) )
    {
        pContext->cbEvents.signalStrengthChangedCallback( urcEvent, pSignalInfo,
                                                          pContext->cbEvents.pSignalStrengthChangedCallbackContext );
    }
}

/*-----------------------------------------------------------*/

void _Cellular_GenericCallback( const CellularContext_t * pContext,
                                const char * pRawData )
{
    if( ( pContext != NULL ) && ( pContext->cbEvents.genericCallback != NULL ) )
    {
        pContext->cbEvents.genericCallback( pRawData, pContext->cbEvents.pGenericCallbackContext );
    }
}

/*-----------------------------------------------------------*/

void _Cellular_ModemEventCallback( const CellularContext_t * pContext,
                                   CellularModemEvent_t modemEvent )
{
    if( ( pContext != NULL ) && ( pContext->cbEvents.modemEventCallback != NULL ) )
    {
        pContext->cbEvents.modemEventCallback( modemEvent, pContext->cbEvents.pModemEventCallbackContext );
    }
}

/*-----------------------------------------------------------*/

CellularSocketContext_t * _Cellular_GetSocketData( const CellularContext_t * pContext,
                                                   uint32_t sockIndex )
{
    CellularSocketContext_t * pSocketData = NULL;

    if( pContext == NULL )
    {
        LogError( ( "Invalid context" ) );
    }
    else
    {
        if( ( sockIndex >= CELLULAR_NUM_SOCKET_MAX ) || ( pContext->pSocketData[ sockIndex ] == NULL ) )
        {
            LogError( ( "_Cellular_GetSocketData, invalid socket handle %u", ( unsigned int ) sockIndex ) );
        }
        else
        {
            pSocketData = pContext->pSocketData[ sockIndex ];
        }
    }

    return pSocketData;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_LibInit( CellularHandle_t * pCellularHandle,
                                   const CellularCommInterface_t * pCommInterface,
                                   const CellularTokenTable_t * pTokenTable )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularContext_t * pContext = NULL;
    bool bLibStatusMutexCreateSuccess = false;
    bool bAtDataMutexCreateSuccess = false;
    bool bPktRequestMutexCreateSuccess = false;
    bool bPktResponseMutexCreateSuccess = false;

    cellularStatus = checkInitParameter( pCellularHandle, pCommInterface, pTokenTable );

    if( cellularStatus != CELLULAR_SUCCESS )
    {
        LogError( ( "_Cellular_CommonInit checkInitParameter failed" ) );
    }
    else
    {
        pContext = _Cellular_AllocContext();

        if( pContext == NULL )
        {
            LogError( ( "CellularContext_t allocation failed" ) );
            cellularStatus = CELLULAR_NO_MEMORY;
        }
        else
        {
            /* Assign the comm interface to pContext. */
            pContext->pCommIntf = pCommInterface;

            /* copy the token table. */
            ( void ) memcpy( &( pContext->tokenTable ), pTokenTable, sizeof( CellularTokenTable_t ) );
        }
    }

    /* Defines the Mutexes and Semaphores. */
    if( cellularStatus == CELLULAR_SUCCESS )
    {
        if( _Cellular_CreateLibStatusMutex( pContext ) != true )
        {
            LogError( ( "Could not create CellularLib status mutex" ) );
            cellularStatus = CELLULAR_RESOURCE_CREATION_FAIL;
        }
        else
        {
            bLibStatusMutexCreateSuccess = true;
        }
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        if( _Cellular_CreateAtDataMutex( pContext ) != true )
        {
            LogError( ( "Could not create CELLULAR AT Data mutex " ) );
            cellularStatus = CELLULAR_RESOURCE_CREATION_FAIL;
        }
        else
        {
            bAtDataMutexCreateSuccess = true;
        }
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        if( _Cellular_CreatePktRequestMutex( pContext ) != true )
        {
            LogError( ( "Could not create CELLULAR Pkt Req mutex " ) );
            cellularStatus = CELLULAR_RESOURCE_CREATION_FAIL;
        }
        else
        {
            bPktRequestMutexCreateSuccess = true;
        }
    }

    if( cellularStatus == CELLULAR_SUCCESS )
    {
        if( _Cellular_CreatePktResponseMutex( pContext ) != true )
        {
            LogError( ( "Could not create CELLULAR Pkt Resp mutex " ) );
            cellularStatus = CELLULAR_RESOURCE_CREATION_FAIL;
        }
        else
        {
            bPktResponseMutexCreateSuccess = true;
        }
    }

    /* Configure the library. */
    if( cellularStatus == CELLULAR_SUCCESS )
    {
        cellularStatus = libOpen( pContext );
    }

    if( cellularStatus != CELLULAR_SUCCESS )
    {
        if( bPktResponseMutexCreateSuccess == true )
        {
            _Cellular_DestroyPktResponseMutex( pContext );
        }

        if( bPktRequestMutexCreateSuccess == true )
        {
            _Cellular_DestroyPktRequestMutex( pContext );
        }

        if( bAtDataMutexCreateSuccess == true )
        {
            _Cellular_DestroyAtDataMutex( pContext );
        }

        if( bLibStatusMutexCreateSuccess == true )
        {
            _Cellular_DestroyLibStatusMutex( pContext );
        }

        if( pContext != NULL )
        {
            _Cellular_FreeContext( pContext );
        }
    }
    else
    {
        *pCellularHandle = pContext;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_LibCleanup( CellularHandle_t cellularHandle )
{
    CellularContext_t * pContext = ( CellularContext_t * ) cellularHandle;
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else
    {
        libClose( pContext );

        _Cellular_DestroyLibStatusMutex( pContext );
        _Cellular_DestroyAtDataMutex( pContext );
        _Cellular_DestroyPktRequestMutex( pContext );
        _Cellular_DestroyPktResponseMutex( pContext );
        _Cellular_FreeContext( pContext );
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularPktStatus_t _Cellular_AtcmdRequestWithCallback( CellularContext_t * pContext,
                                                        CellularAtReq_t atReq )
{
    /* Parameters are checked in this function. */
    return _Cellular_TimeoutAtcmdRequestWithCallback( pContext, atReq, ( uint32_t ) PACKET_REQ_TIMEOUT_MS );
}

/*-----------------------------------------------------------*/

CellularPktStatus_t _Cellular_TimeoutAtcmdRequestWithCallback( CellularContext_t * pContext,
                                                               CellularAtReq_t atReq,
                                                               uint32_t timeoutMS )
{
    return _Cellular_PktHandler_AtcmdRequestWithCallback( pContext, atReq, timeoutMS );
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_RegisterUndefinedRespCallback( CellularContext_t * pContext,
                                                         CellularUndefinedRespCallback_t undefinedRespCallback,
                                                         void * pCallbackContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        LogError( ( "_Cellular_RegisterUndefinedRespCallback: invalid context" ) );
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else
    {
        /* undefinedRespCallback can be set to NULL to unregister the callback. */
        PlatformMutex_Lock( &( pContext->PktRespMutex ) );
        pContext->undefinedRespCallback = undefinedRespCallback;

        if( pContext->undefinedRespCallback != NULL )
        {
            pContext->pUndefinedRespCBContext = pCallbackContext;
        }
        else
        {
            pContext->pUndefinedRespCBContext = NULL;
        }

        PlatformMutex_Unlock( &( pContext->PktRespMutex ) );
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

CellularError_t _Cellular_RegisterInputBufferCallback( CellularContext_t * pContext,
                                                       CellularInputBufferCallback_t inputBufferCallback,
                                                       void * pInputBufferCallbackContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        LogError( ( "_Cellular_RegisterUrcDataCallback: invalid context." ) );
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else
    {
        /* inputBufferCallback can be set to NULL to unregister the callback. */
        PlatformMutex_Lock( &( pContext->PktRespMutex ) );
        pContext->inputBufferCallback = inputBufferCallback;

        if( pContext->inputBufferCallback != NULL )
        {
            pContext->pInputBufferCallbackContext = pInputBufferCallbackContext;
        }
        else
        {
            pContext->pInputBufferCallbackContext = NULL;
        }

        PlatformMutex_Unlock( &( pContext->PktRespMutex ) );
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/
