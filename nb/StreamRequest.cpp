//==============================================================================
//
//  StreamRequest.cpp
//
//  Copyright (C) 2013-2020  Greg Utas
//
//  This file is part of the Robust Services Core (RSC).
//
//  RSC is free software: you can redistribute it and/or modify it under the
//  terms of the GNU General Public License as published by the Free Software
//  Foundation, either version 3 of the License, or (at your option) any later
//  version.
//
//  RSC is distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
//  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
//  details.
//
//  You should have received a copy of the GNU General Public License along
//  with RSC.  If not, see <http://www.gnu.org/licenses/>.
//
#include "StreamRequest.h"
#include <cstddef>
#include <string>
#include "Debug.h"
#include "Formatters.h"

using std::ostream;
using std::string;

//------------------------------------------------------------------------------

namespace NodeBase
{
fn_name StreamRequest_ctor1 = "StreamRequest.ctor";

StreamRequest::StreamRequest() : stream_(nullptr)
{
   Debug::ft(StreamRequest_ctor1);
}

//------------------------------------------------------------------------------

fn_name StreamRequest_ctor2 = "StreamRequest.ctor(copy)";

StreamRequest::StreamRequest(const StreamRequest& that) : MsgBuffer(that)
{
   Debug::ft(StreamRequest_ctor2);
}

//------------------------------------------------------------------------------

fn_name StreamRequest_dtor = "StreamRequest.dtor";

StreamRequest::~StreamRequest()
{
   Debug::ftnt(StreamRequest_dtor);
}

//------------------------------------------------------------------------------

fn_name StreamRequest_Cleanup = "StreamRequest.Cleanup";

void StreamRequest::Cleanup()
{
   Debug::ft(StreamRequest_Cleanup);

   stream_.reset();
   written_.reset();
   MsgBuffer::Cleanup();
}

//------------------------------------------------------------------------------

void StreamRequest::Display(ostream& stream,
   const string& prefix, const Flags& options) const
{
   MsgBuffer::Display(stream, prefix, options);

   stream << prefix << "stream : " << stream_.get() << CRLF;

   //  Display the first four lines of stream_.
   //
   if(stream_ != nullptr)
   {
      const auto& str = stream_->str();
      auto indent = prefix + spaces(2);

      for(size_t i = 0, begin = 0; i < 4; ++i)
      {
         auto end = str.find(CRLF, begin);

         if(end == string::npos)
         {
            stream << indent << str.substr(begin) << CRLF;
            break;
         }

         stream << indent << str.substr(begin, end - begin + 1);
         begin = end + 1;
         if(begin >= str.size()) break;
      }
   }

   stream << prefix << "written : " << written_.get() << CRLF;
}

//------------------------------------------------------------------------------

void StreamRequest::Patch(sel_t selector, void* arguments)
{
   MsgBuffer::Patch(selector, arguments);
}
}
