// Copyright (c) 2021, University of Washington
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the University of Washington nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF WASHINGTON AND CONTRIBUTORS
// “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF WASHINGTON OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import https from 'https';
import netrc from 'netrc';
import pkg from './package.json' assert { type: 'json' };
import path from 'path';

//------------------------------------
// File Data
//------------------------------------

//
// System Credentials
//
let sysCredentials = {
  refresh: null,
  access: null,
  expiration: 0,
};

//
// System Configuration
//
let sysConfig = {
  domain: "slideruleearth.io",
  organization: "sliderule",
  protocol: https,
  verbose: true,
  desired_nodes: null,
  time_to_live: 60,
  timeout: 120000, // milliseconds
};

//
// Record Definitions
//
let recordDefinitions = {}

//
// SlideRule Constants
//
const INT8            = 0;
const INT16           = 1;
const INT32           = 2;
const INT64           = 3;
const UINT8           = 4;
const UINT16          = 5;
const UINT32          = 6;
const UINT64          = 7;
const BITFIELD        = 8;
const FLOAT           = 9;
const DOUBLE          = 10;
const TIME8           = 11;
const STRING          = 12;
const USER            = 13;

//
// SlideRule Field Types
//
const fieldtypes = {
  INT8:     {code: INT8,      size: 1},
  INT16:    {code: INT16,     size: 2},
  INT32:    {code: INT32,     size: 4},
  INT64:    {code: INT64,     size: 8},
  UINT8:    {code: UINT8,     size: 1},
  UINT16:   {code: UINT16,    size: 2},
  UINT32:   {code: UINT32,    size: 4},
  UINT64:   {code: UINT64,    size: 8},
  BITFIELD: {code: BITFIELD,  size: 1},
  FLOAT:    {code: FLOAT,     size: 4},
  DOUBLE:   {code: DOUBLE,    size: 8},
  TIME8:    {code: TIME8,     size: 8},
  STRING:   {code: STRING,    size: 1},
  USER:     {code: USER,      size: 0},
};

//------------------------------------
// Local Functions
//------------------------------------

//
// populateDefinition
//
function populateDefinition(rec_type) {
  if (rec_type in recordDefinitions) {
    return recordDefinitions[rec_type];
  }
  else {
    return new Promise((resolve, reject) => {
      source("definition", {"rectype" : rec_type}).then(
        result => {
          recordDefinitions[rec_type] = result;
          resolve(recordDefinitions[rec_type]);
        },
        error => {
          reject(new Error(`failed to retrieve definition for ${rec_type}: ${error}`));
        }
      );
    });
  }
}

//
// getFieldSize
//
async function getFieldSize(type) {
  if (type in fieldtypes) {
    return fieldtypes[type].size;
  }
  else {
    let rec_def = await populateDefinition(type);
    return rec_def.__datasize;
  }
}

//
// decodeElement
//
function decodeElement(type_code, big_endian, buffer, byte_offset) {
  if (big_endian) {
    switch (type_code) {
      case INT8:      return buffer.readInt8(byte_offset);
      case INT16:     return buffer.readInt16BE(byte_offset);
      case INT32:     return buffer.readInt32BE(byte_offset);
      case INT64:     return buffer.readBigInt64BE(byte_offset);
      case UINT8:     return buffer.readUInt8(byte_offset);
      case UINT16:    return buffer.readUInt16BE(byte_offset);
      case UINT32:    return buffer.readUInt32BE(byte_offset);
      case UINT64:    return buffer.readBigUInt64BE(byte_offset);
      case BITFIELD:  throw new Error(`Bit fields are unsupported`);
      case FLOAT:     return buffer.readFloatBE(byte_offset);
      case DOUBLE:    return buffer.readDoubleBE(byte_offset);
      case TIME8:     return new Date(Number(buffer.readBigInt64BE(byte_offset) / BigInt(1000000)));
      case STRING:    return String.fromCharCode(buffer.readUInt8(byte_offset));
      case USER:      throw new Error(`User fields cannot be decoded as a primitive`);
      default:        throw new Error(`Invalid field type ${type_code}`);
    }
  }
  else {
    switch (type_code) {
      case INT8:      return buffer.readInt8(byte_offset);
      case INT16:     return buffer.readInt16LE(byte_offset);
      case INT32:     return buffer.readInt32LE(byte_offset);
      case INT64:     return buffer.readBigInt64LE(byte_offset);
      case UINT8:     return buffer.readUInt8(byte_offset);
      case UINT16:    return buffer.readUInt16LE(byte_offset);
      case UINT32:    return buffer.readUInt32LE(byte_offset);
      case UINT64:    return buffer.readBigUInt64LE(byte_offset);
      case BITFIELD:  throw new Error(`Bit fields are unsupported`);
      case FLOAT:     return buffer.readFloatLE(byte_offset);
      case DOUBLE:    return buffer.readDoubleLE(byte_offset);
      case TIME8:     return new Date(Number(buffer.readBigInt64LE(byte_offset) / BigInt(1000000)));
      case STRING:    return String.fromCharCode(buffer.readUInt8(byte_offset));
      case USER:      throw new Error(`User fields cannot be decoded as a primitive`);
      default:        throw new Error(`Invalid field type ${type_code}`);
    };
  }
}

//
// decodeField
//
async function decodeField(field_def, buffer, offset, rec_size) {
  let value = []; // ultimately returned

  // Pull out field attributes
  let big_endian = (field_def.flags.match('BE') != null);
  let byte_offset = offset + (field_def.offset / 8);
  let num_elements = field_def.elements;
  let field_size = await getFieldSize(field_def.type);

  // Get type code
  let type_code = USER;
  if (field_def.type in fieldtypes) {
     type_code = fieldtypes[field_def.type].code;
  }

  // For variable length fields, recalculate number of elements using size of record
  if (num_elements == 0) {
    num_elements = (rec_size - byte_offset) / field_size;
  }

  // Decode elements
  for (let i = 0; i < num_elements; i++) {
    if (field_def.type in fieldtypes) {
      value.push(decodeElement(type_code, big_endian, buffer, byte_offset));
    }
    else {
      value.push(await decodeRecord(field_def.type, buffer, byte_offset, rec_size));
    }
    byte_offset += field_size;
  }

  // Create final value
  if (type_code == STRING) {
    value = value.join('');
    let null_index = value.indexOf('\0');
    if (null_index > -1) {
      value = value.substr(0, null_index);
    }
  }
  else if (num_elements == 1) {
    value = value[0];
  }

  // Return value
  return value;
}

//
// decodeRecord
//
async function decodeRecord(rec_type, buffer, offset, rec_size) {
  let rec_obj = {}
  let rec_def = await populateDefinition(rec_type);
  // For each field defined in record
  for (let field in rec_def) {
    // Check if not property
    if (field.match(/^__/) == null) {
      rec_obj[field] = await decodeField(rec_def[field], buffer, offset, rec_size);
    }
  }
  // Return decoded record
  return rec_obj;
}

// Use dynamic import based on the environment
let fetch;
if (typeof window === 'undefined') {
    // Node.js environment
    fetch = (await import('node-fetch')).default;
} else {
    // Browser environment
    fetch = window.fetch;
}

async function fetchAndProcessStream(url, options, callbacks={}) {
  try {
      console.log('url:', url);
      console.log('options:', options);
      console.log('callbacks:', callbacks);
      // Fetch the resource
      const response = await fetch(url, options);
      console.log('response:', response);
      // Check if the response is ok (status in the range 200-299)
      if (!response.ok) {
          throw new Error(`HTTP error! status: ${response.status}`);
      }

      // Examine the headers from the response
      for (const [key, value] of response.headers.entries()) {
          console.log(`${key}: ${value}`);
      }
      const contentType = response.headers.get('content-type');

      // Get the reader from the response stream
      const reader = response.body.getReader();

      // Process the stream
      let receivedLength = 0; // length of the received  data
      let chunks = []; // array to store received  chunks
      const REC_HDR_SIZE = 8;
      const REC_VERSION = 2;
      let results = {};
      let total_bytes_read = null;
      let bytes_read = 0;
      let bytes_processed = 0;
      let bytes_to_process = 0;
      let got_header = false;
      let rec_size = 0;
      let rec_type_size = 0;

      while (true) {
        const { done, chunk } = await reader.read();

        if (done) {
            // The stream has been read completely
            total_bytes_read = bytes_read;
            results["bytes_read"] = total_bytes_read;
            results["bytes_processed"] = bytes_processed;
            if (contentType == 'application/octet-stream') {
              results["bytes_read"] = total_bytes_read;
              results["bytes_processed"] = bytes_processed;
            } else if (contentType == 'application/json' || contentType == 'text/plain') {
            }
            break;
        }
        console.log('done:', done);
        console.log('chunk:', chunk);
        console.log(`Received ${chunk.length} bytes of data`);
        chunks.push(chunk);
        receivedLength += chunk.length;

        if (contentType == 'application/octet-stream') {
          bytes_read += chunk.length;
          bytes_to_process += chunk.length;
          while (bytes_to_process > 0) {
            // State: Accumulating Header
            if (!got_header && bytes_to_process > REC_HDR_SIZE) {
              // Process header
              got_header = true;
              bytes_processed += REC_HDR_SIZE;
              bytes_to_process -= REC_HDR_SIZE;
              let buffer = Buffer.concat(chunks);
              // Get header info
              let rec_version = buffer.readUInt16BE(0);
              rec_type_size = buffer.readUInt16BE(2);
              let rec_data_size = buffer.readUInt32BE(4);
              if (rec_version != REC_VERSION) {
                reject(new Error(`invalid record format: ${rec_version}`))
              }
              // Set record attributes
              rec_size = rec_type_size + rec_data_size;
              chunks = [buffer.subarray(REC_HDR_SIZE)];
            }
            // State: Accumulating Record
            else if (got_header && bytes_to_process >= rec_size) {
              // Process record
              got_header = false;
              bytes_to_process -= rec_size;
              bytes_processed += rec_size;
              let buffer = Buffer.concat(chunks);
              let rec_type = buffer.toString('utf8', 0, rec_type_size - 1);
              decodeRecord(rec_type, buffer, rec_type_size, rec_size).then(
                result => {
                  if (rec_type in callbacks) {
                    callbacks[rec_type](result);
                  }
                }
              );
              // Update stats
              if (!(rec_type in results)) {
                results[rec_type] = 0;
              }
              results[rec_type]++;
              // Check if complete
              if ((total_bytes_read != null) && (bytes_processed == total_bytes_read)) {
                results["bytes_processed"] = bytes_processed;
                resolve(results);
              }
              // Restore unused bytes that have been read
              if(bytes_to_process > 0) {
                chunks = [buffer.subarray(rec_size)];
              }
              else {
                chunks = [];
              }
            }
            // State: Need More Data
            else {
              break;
            }
          }
        }  
      }
      if (contentType == 'application/octet-stream') {

        // Combine chunks into a single Uint8Array
        let binaryData = new Uint8Array(receivedLength);
        let position = 0;
        for (let chunk of chunks) {
            binaryData.set(chunk, position);
            position += chunk.length;
        }
        return binaryData;

      } else if (contentType == 'application/json' || contentType == 'text/plain') {
        let buffer = Buffer.concat(chunks);
        let jsonData = JSON.parse(buffer);
        return jsonData;
      }
  } catch (error) {
      // Handle any errors
      console.error('Error fetching or processing stream:', error);
      throw error; // Re-throw the error if you want to handle it further up the call stack
  }
}

// Example usage
// fetchAndProcessStream('https://example.com/some-binary-data')
//   .then(data => console.log('Received binary data:', data))
//   .catch(error => console.error('Failed to fetch or process stream:', error));

//------------------------------------
// Exported Functions
//------------------------------------

//
// Initialize Client
//
export function init(config) {
  sysConfig = Object.assign(sysConfig, config)
}

//
// Source Endpoint
//
// export function source(api, parm=null, stream=false, callbacks={}) {
//   // Setup Request Options
//   const options = {
//     host: sysConfig.organization && (sysConfig.organization + '.' + sysConfig.domain) || sysConfig.domain,
//     path: '/source/' + api,
//     method: stream && 'POST' || 'GET',
//   };
//   // Build Body
//   let body = null;
//   if (parm != null) {
//     body = JSON.stringify(parm);
//     options["headers"] = {'Content-Type': 'application/json', 'Content-Length': body.length};
//   }
//   // Make API Request
//   return httpRequest(options, body, callbacks);
// }
export function source(api, parm=null, stream=false, callbacks={}) {
  const host = sysConfig.organization && (sysConfig.organization + '.' + sysConfig.domain) || sysConfig.domain;
  //console.log('host: ', host);
  //console.log('sysConfig: ', sysConfig);
  //console.log('sysConfig.organization: ', sysConfig.organization);
  const api_path = path.join('source', api);
  //console.log('api_path: ', api_path);
  console.log('parm: ', parm);
  const url = 'https://' + host + '/' + api_path;
  console.log('url: ', url);
  // Setup Request Options
  // When stream is true, options.method will be 'POST'.
  // When stream is false, options.method will be 'GET'.
  let options = {
    method: stream ? 'POST' : 'GET',
  };

  let body = null;
  if (parm != null) {
    body = JSON.stringify(parm);
    options.headers = {
      'Content-Type': 'application/json', 
      'Content-Length': Buffer.byteLength(body)
    };
    options.body = body;
  }
  console.log('options: ', options);
  // Make API Request
  //return fetchRequest(url, options, body, callbacks);
  return fetchAndProcessStream(url, options, callbacks);
}


//
// Authenticate User
//
// export function authenticate(ps_username=null, ps_password=null) {
//     // Build Provisioning System URL
//     let psHost = 'ps.' + sysConfig.domain;
//     // Obtain Username and Password
//     ps_username = ps_username ?? process.env.PS_USERNAME;
//     ps_password = ps_password ?? process.env.PS_PASSWORD;
//     if (ps_username == null || ps_password == null) {
//       let myNetrc = netrc();
//       if (psHost in myNetrc) {
//         ps_username = myNetrc[psHost].login;
//         ps_password = myNetrc[psHost].password;
//       }
//     }
//     // Bail If Username and Password Not Found
//     if(!ps_username && !ps_password) {
//       console.error(`Unable to obtain username and/or password for ${psHost}`);
//       return Promise.resolve();
//     }
//     // Build Request Body
//     let body = JSON.stringify({username: ps_username, password: ps_password, org_name: sysConfig.organization});
//     // Setup Request Options
//     const options = {
//       host: psHost,
//       path: '/api/org_token/',
//       method: 'POST',
//       headers: {'Content-Type': 'application/json', 'Content-Length': body.length},
//     };

//     // Make Authentication Request
//     return httpRequest(options, body).then(
//       result => {
//         let expiration = 0;
//         try {
//           sysCredentials.access = result.access;
//           sysCredentials.refresh = result.refresh;
//           sysCredentials.expiration =  (Date.now() / 1000) + (result.access_lifetime / 2);
//           expiration = sysCredentials.expiration;
//         }
//         catch (e) {
//           console.error("Error processing authentication response\n", result);
//         }
//         return expiration;
//       }
//     );
// }

//
// Get Version
//
export function get_version() {
  return source('version').then(
    result => {
      result['client'] = {version: pkg['version']};
      result['organization'] = sysConfig.organization;
      return result;
    }
  );
}

//
// Get Values
//
export function get_values(bytearray, fieldtype) {
  let values = [];
  let buffer = Buffer.from(bytearray);
  switch (fieldtype) {
    case INT8:    for (let i = 0; i < buffer.length; i += 1) values.push(buffer.readInt8LE(i));   break;
    case INT16:   for (let i = 0; i < buffer.length; i += 2) values.push(buffer.readInt16LE(i));  break;
    case INT32:   for (let i = 0; i < buffer.length; i += 4) values.push(buffer.readInt32LE(i));  break;
    case INT64:   for (let i = 0; i < buffer.length; i += 8) values.push(buffer.readInt64LE(i));  break;
    case UINT8:   for (let i = 0; i < buffer.length; i += 1) values.push(buffer.readUInt8LE(i));  break;
    case UINT16:  for (let i = 0; i < buffer.length; i += 2) values.push(buffer.readUInt16LE(i)); break;
    case UINT32:  for (let i = 0; i < buffer.length; i += 4) values.push(buffer.readUInt32LE(i)); break;
    case UINT64:  for (let i = 0; i < buffer.length; i += 8) values.push(buffer.readUInt64LE(i)); break;
    case FLOAT:   for (let i = 0; i < buffer.length; i += 4) values.push(buffer.readFloatLE(i));  break;
    case DOUBLE:  for (let i = 0; i < buffer.length; i += 8) values.push(buffer.readDoubleLE(i)); break;
    case TIME8:   for (let i = 0; i < buffer.length; i += 8) values.push(buffer.readInt64LE(i));  break;
    case STRING:  values = String.fromCharCode.apply(null, bytearray); break;
    default: break;
  }
  return values;
}
