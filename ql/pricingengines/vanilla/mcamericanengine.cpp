/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2006 Klaus Spanderen

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

/*! \file mcamericanengine.cpp
    \brief Monte Carlo engine for vanilla american options
*/

#include <ql/pricingengines/vanilla/mcamericanengine.hpp>
#include <ql/errors.hpp>
#include <ql/math/functional.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/bind.hpp>

namespace QuantLib {

    AmericanPathPricer::AmericanPathPricer(
        const ext::shared_ptr<Payoff>& payoff,
        Size polynomOrder,
        LsmBasisSystem::PolynomType polynomType)
    : scalingValue_(1.0),
      payoff_      (payoff),
      v_           (LsmBasisSystem::pathBasisSystem(polynomOrder,
                                                    polynomType)) {

        QL_REQUIRE(   polynomType == LsmBasisSystem::Monomial
                   || polynomType == LsmBasisSystem::Laguerre
                   || polynomType == LsmBasisSystem::Hermite
                   || polynomType == LsmBasisSystem::Hyperbolic
                   || polynomType == LsmBasisSystem::Chebyshev2nd,
                   "insufficient polynom type");

        using namespace ext::placeholders;

        // the payoff gives an additional value
        v_.emplace_back(ext::bind(&AmericanPathPricer::payoff, this, _1));

        const ext::shared_ptr<StrikedTypePayoff> strikePayoff
            = ext::dynamic_pointer_cast<StrikedTypePayoff>(payoff_);

        if (strikePayoff) {
            // FLOATING_POINT_EXCEPTION
            scalingValue_/=strikePayoff->strike();
        }
    }

    Real AmericanPathPricer::payoff(Real state) const {
        return (*payoff_)(state/scalingValue_);
    }

    Real AmericanPathPricer::operator()(const Path& path, Size t) const {
        return payoff(state(path, t));
    }

    Real AmericanPathPricer::state(const Path& path, Size t) const {
        // scale values of the underlying
        // to increase numerical stability
        return path[t]*scalingValue_;
    }

    std::vector<ext::function<Real(Real)> >
    AmericanPathPricer::basisSystem() const {
        return v_;
    }

}
