/*
 * Licensed to the University of Washington under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The University of Washington
 * licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef __ccsdspkg__
#define __ccsdspkg__

/******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include "CcsdsPacket.h"
#include "CcsdsPacketizer.h"
#include "CcsdsPacketInterleaver.h"
#include "CcsdsPacketParser.h"
#include "CcsdsParserAOSFrameModule.h"
#include "CcsdsParserModule.h"
#include "CcsdsParserStripModule.h"
#include "CcsdsParserZFrameModule.h"
#include "CcsdsPayloadDispatch.h"
#include "CcsdsRecord.h"
#include "CcsdsRecordDispatcher.h"

/******************************************************************************
 * PROTOTYPES
 ******************************************************************************/

void initccsds (void);
void deinitccsds (void);

#endif  /* __ccsdspkg__ */


