/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2004, 2008 StatPro Italia srl
 Copyright (C) 2004 Decillion Pty(Ltd)

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

#include <ql/exchangerate.hpp>

namespace QuantLib {

    Money ExchangeRate::exchange(const Money& amount) const {
        switch (type_) {
            case Direct:
                if (amount.currency() == source_)
                    return Money(amount.value() * rate_, target_);
                else if (amount.currency() == target_)
                    return Money(amount.value() / rate_, source_);
                else
                    QL_FAIL("exchange rate not applicable");
            case Derived: {
                auto& [ex1, ex2] = rateChain_.value();
                if (amount.currency() == ex1->source() || amount.currency() == ex1->target())
                    return ex2->exchange(ex1->exchange(amount));
                else if (amount.currency() == ex2->source() || amount.currency() == ex2->target())
                    return ex1->exchange(ex2->exchange(amount));
                else
                    QL_FAIL("exchange rate not applicable");
            }
            default:
                QL_FAIL("unknown exchange-rate type");
        }
    }

    ExchangeRate::ExchangeRate(const ExchangeRate& r1, const ExchangeRate& r2)
    : type_(Derived), rateChain_(std::make_pair(std::make_shared<ExchangeRate>(r1),
                                                std::make_shared<ExchangeRate>(r2))) {
        if (r1.source_ == r2.source_) {
            source_ = r1.target_;
            target_ = r2.target_;
            rate_ = r2.rate_ / r1.rate_;
        } else if (r1.source_ == r2.target_) {
            source_ = r1.target_;
            target_ = r2.source_;
            rate_ = 1.0 / (r1.rate_ * r2.rate_);
        } else if (r1.target_ == r2.source_) {
            source_ = r1.source_;
            target_ = r2.target_;
            rate_ = r1.rate_ * r2.rate_;
        } else if (r1.target_ == r2.target_) {
            source_ = r1.source_;
            target_ = r2.source_;
            rate_ = r1.rate_ / r2.rate_;
        } else {
            QL_FAIL("exchange rates not chainable");
        }
    }


    ExchangeRate ExchangeRate::chain(const ExchangeRate& r1, const ExchangeRate& r2) {
        return ExchangeRate(r1, r2);
    }

}
