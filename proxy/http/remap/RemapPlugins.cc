/** @file

  Class to execute one (or more) remap plugin(s).

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

 */

#include "RemapPlugins.h"

ClassAllocator<RemapPlugins> pluginAllocator("RemapPluginsAlloc");

TSRemapStatus
RemapPlugins::run_plugin(RemapPluginInst *plugin)
{
  ink_assert(_s);

  TSRemapStatus plugin_retcode;
  TSRemapRequestInfo rri;
  URL *map_from = _s->url_map.getFromURL();
  URL *map_to   = _s->url_map.getToURL();

  // This is the equivalent of TSHttpTxnClientReqGet(), which every remap plugin would
  // have to call.
  rri.requestBufp = reinterpret_cast<TSMBuffer>(_request_header);
  rri.requestHdrp = reinterpret_cast<TSMLoc>(_request_header->m_http);

  // Read-only URL's (TSMLoc's to the SDK)
  rri.mapFromUrl = reinterpret_cast<TSMLoc>(map_from->m_url_impl);
  rri.mapToUrl   = reinterpret_cast<TSMLoc>(map_to->m_url_impl);
  rri.requestUrl = reinterpret_cast<TSMLoc>(_request_url->m_url_impl);

  rri.redirect = 0;

  // Prepare State for the future
  if (_cur == 0) {
    _s->os_response_plugin_inst = plugin;
  }

  HttpTransact::milestone_start_api_time(_s);
  plugin_retcode = plugin->doRemap(reinterpret_cast<TSHttpTxn>(_s->state_machine), &rri);
  HttpTransact::milestone_update_api_time(_s);

  // TODO: Deal with negative return codes here
  if (plugin_retcode < 0) {
    plugin_retcode = TSREMAP_NO_REMAP;
  }

  // First step after plugin remap must be "redirect url" check
  if ((TSREMAP_DID_REMAP == plugin_retcode || TSREMAP_DID_REMAP_STOP == plugin_retcode) && rri.redirect) {
    _s->remap_redirect = _request_url->string_get(nullptr);
  }

  return plugin_retcode;
}

/**
  This is the equivalent of the old DoRemap().

  @return 1 when you are done doing crap (otherwise, you get re-called
    with schedule_imm and i hope you have something more to do), else
    0 if you have something more to do (this isn't strict and we check
    there actually *is* something to do).

*/
bool
RemapPlugins::run_single_remap()
{
  url_mapping *map             = _s->url_map.getMapping();
  RemapPluginInst *plugin      = map->get_plugin_instance(_cur); // get the nth plugin in our list of plugins
  TSRemapStatus plugin_retcode = TSREMAP_NO_REMAP;
  bool zret                    = true; // default - last iteration.
  Debug("url_rewrite", "running single remap rule id %d for the %d%s time", map->map_id, _cur,
        _cur == 1 ? "st" :
        _cur == 2 ? "nd" :
        _cur == 3 ? "rd" :
                    "th");

  if (0 == _cur) {
    Debug("url_rewrite", "setting the remapped url by copying from mapping rule");
    url_rewrite_remap_request(_s->url_map, _request_url, _s->hdr_info.client_request.method_get_wksidx());
  }

  // There might not be a plugin if we are a regular non-plugin map rule. In that case, we will fall through
  // and do the default mapping and then stop.
  if (plugin) {
    plugin_retcode = run_plugin(plugin);
  }

  ++_cur;

  // If the plugin redirected, we need to end the remap chain now. Otherwise see what's next.
  if (!_s->remap_redirect) {
    if (TSREMAP_DID_REMAP_STOP == plugin_retcode || TSREMAP_DID_REMAP == plugin_retcode) {
      ++_rewritten;
    }

    if (TSREMAP_NO_REMAP_STOP == plugin_retcode || TSREMAP_DID_REMAP_STOP == plugin_retcode) {
      Debug("url_rewrite", "breaking remap plugin chain since last plugin said we should stop after %d rewrites", _rewritten);
    } else if (_cur >= map->plugin_instance_count()) {
      Debug("url_rewrite", "completed all remap plugins for rule id %d, changed by %d plugins", map->map_id, _rewritten);
    } else {
      Debug("url_rewrite", "completed single remap, attempting another via immediate callback");
      zret = false; // not done yet.
    }
  }
  return zret;
}
