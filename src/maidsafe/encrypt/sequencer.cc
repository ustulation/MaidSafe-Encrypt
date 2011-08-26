﻿/*******************************************************************************
 *  Copyright 2008-2011 maidsafe.net limited                                   *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the license   *
 *  file LICENSE.TXT found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 ***************************************************************************//**
 * @file  sequencer.cc
 * @brief random access buffer.
 * @date  2011-08-14
 */


#include "maidsafe/encrypt/sequencer.h"

 
 namespace maidsafe {
 namespace encrypt {
 
 bool Sequencer::Add(size_t position, char* data, size_t length) {
   // TODO (dirvine) if a write happens half way through we count as 2 sets,
   // need to take
   // care of this in the getFromSequencer method.
   // ah no needs to be here, otherwise we lose timeline

     auto iter = sequencer_.find(position);
     if (iter == sequencer_.end()) {
       try {
         auto it = sequencer_.begin();
       sequencer_.insert(it, std::pair<size_t, sequence_data>
         (position, sequence_data(data, length)));
       } catch (std::exception &e) {
         return false;
       }
     } else {
       (*iter).second.first = data;
       (*iter).second.second = length;
   }
   return true;
 }
 
 sequence_data Sequencer::PositionFromSequencer(size_t position, bool remove) {
   if (sequencer_.size() == 0)
     return (sequence_data(static_cast<char*>(NULL), 0));
   for (auto it = sequencer_.begin(); it != sequencer_.end(); ++it) {
     size_t this_position = (*it).first;
     char * this_data = (*it).second.first;
     size_t this_length = (*it).second.second;
     // got the data - it is contiguous
     if (this_position == position) {
       sequence_data result = sequence_data(this_data,
                                            this_length);
       if (remove)
         sequencer_.erase(it);
       return result;
     }
     // get some data that's inside a chunk of sequenced data
     if (this_position + this_length  >= position) {
       // get address of element and length
       sequence_data res(&this_data[position - this_position],
                      this_length - (position - this_position));

       if (remove) {
       // get the remaining data add again with Add
       Add(this_position,
           &this_data[position - this_position],
           this_length-position - this_position);
       sequencer_.erase(it); // remove this element
       }
       return res;
     }
   }
   return (sequence_data(static_cast<char*>(NULL), 0));  // nothing found
 }

size_t Sequencer::NextFromSequencer(char * data, size_t *length, bool remove) {
  if (sequencer_.size() == 0)
    return (0);
  auto it = sequencer_.begin();
  size_t position = (*it).first;
  data = (*it).second.first;
  *length = (*it).second.second;

  if (remove)
    sequencer_.erase(it);
  return position;
}

}  // namespace encrypt
}  // namespace maidsafe