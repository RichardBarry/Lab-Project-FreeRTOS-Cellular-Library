/*
 * FreeRTOS-Cellular-Interface <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

/* Standard includes. */
#include <stdint.h>

/* Cellular APIs includes. */
#include "cellular_config.h"
#include "cellular_config_defaults.h"
#include "cellular_types.h"
#include "cellular_common_internal.h"
#include "cellular_common_api.h"


/****************************************************************
* The signature of the function under test.
****************************************************************/

CellularError_t _Cellular_ComputeSignalBars( CellularRat_t rat,
                                             CellularSignalInfo_t * pSignalInfo );

/****************************************************************
* The proof of _Cellular_ComputeSignalBars
****************************************************************/
void harness()
{
    CellularRat_t rat;
    CellularSignalInfo_t * pSignalInfo = ( CellularSignalInfo_t * ) safeMalloc( sizeof( CellularSignalInfo_t ) );

    _Cellular_ComputeSignalBars( rat, pSignalInfo );
}
