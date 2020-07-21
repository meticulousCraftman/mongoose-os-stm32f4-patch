/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once  // no_extern_c_check

/*
 * Provides the IRAM macro that creates unique sections under .text
 * so that unused functions can be GC'd.
 */

#define _IRAM_STR_LIT(a) #a
#define _IRAM_STR(a) _IRAM_STR_LIT(a)

#define IRAM \
  __attribute__((section(".text.IRAM." __FILE__ "." _IRAM_STR(__LINE__))))

#define NOINLINE __attribute__((noinline))
