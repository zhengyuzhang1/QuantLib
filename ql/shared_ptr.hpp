/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2018 StatPro Italia srl

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file shared_ptr.hpp
    \brief Maps shared_ptr to either the boost or std implementation
*/

#ifndef quantlib_shared_ptr_hpp
#define quantlib_shared_ptr_hpp

#include <ql/qldefines.hpp>

#include <memory>

namespace QuantLib {

    namespace ext {

        using std::shared_ptr;
        using std::weak_ptr;
        using std::make_shared;
        using std::static_pointer_cast;
        using std::dynamic_pointer_cast;
        using std::enable_shared_from_this;

    }

}


#endif

