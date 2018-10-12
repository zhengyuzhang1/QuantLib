/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2003 RiskMap srl
 Copyright (C) 2004, 2005, 2006, 2007, 2008 StatPro Italia srl

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

#include "capfloor.hpp"
#include "utilities.hpp"
#include <ql/instruments/capfloor.hpp>
#include <ql/instruments/vanillaswap.hpp>
#include <ql/cashflows/cashflowvectors.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/pricingengines/capfloor/blackcapfloorengine.hpp>
#include <ql/pricingengines/swap/discountingswapengine.hpp>
#include <ql/models/marketmodels/models/flatvol.hpp>
#include <ql/models/marketmodels/correlations/expcorrelations.hpp>
#include <ql/math/matrix.hpp>
#include <ql/time/daycounters/actualactual.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/time/schedule.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <ql/cashflows/cashflows.hpp>
#include <ql/cashflows/couponpricer.hpp>
#include <ql/quotes/simplequote.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;

namespace {

    struct CommonVars {
        // common data
        Date settlement;
        std::vector<Real> nominals;
        BusinessDayConvention convention;
        Frequency frequency;
        ext::shared_ptr<IborIndex> index;
        Calendar calendar;
        Natural fixingDays;
        RelinkableHandle<YieldTermStructure> termStructure;

        // cleanup

        SavedSettings backup;

        // setup
        CommonVars()
        : nominals(1,100) {
            frequency = Semiannual;
            index = ext::shared_ptr<IborIndex>(new Euribor6M(termStructure));
            calendar = index->fixingCalendar();
            convention = ModifiedFollowing;
            Date today = calendar.adjust(Date::todaysDate());
            Settings::instance().evaluationDate() = today;
            Natural settlementDays = 2;
            fixingDays = 2;
            settlement = calendar.advance(today,settlementDays,Days);
            termStructure.linkTo(flatRate(settlement,0.05,
                                          ActualActual(ActualActual::ISDA)));
        }

        // utilities
        Leg makeLeg(const Date& startDate, Integer length) {
            Date endDate = calendar.advance(startDate,length*Years,convention);
            Schedule schedule(startDate, endDate, Period(frequency), calendar,
                              convention, convention,
                              DateGeneration::Forward, false);
            return IborLeg(schedule, index)
                .withNotionals(nominals)
                .withPaymentDayCounter(index->dayCounter())
                .withPaymentAdjustment(convention)
                .withFixingDays(fixingDays);
        }

        ext::shared_ptr<PricingEngine> makeEngine(Volatility volatility) {
            Handle<Quote> vol(ext::shared_ptr<Quote>(
                                                new SimpleQuote(volatility)));
            return ext::shared_ptr<PricingEngine>(
                                new BlackCapFloorEngine(termStructure, vol));
        }

        ext::shared_ptr<CapFloor> makeCapFloor(CapFloor::Type type,
                                                 const Leg& leg,
                                                 Rate strike,
                                                 Volatility volatility) {
            ext::shared_ptr<CapFloor> result;
            switch (type) {
              case CapFloor::Cap:
                result = ext::shared_ptr<CapFloor>(
                                  new Cap(leg, std::vector<Rate>(1, strike)));
                break;
              case CapFloor::Floor:
                result = ext::shared_ptr<CapFloor>(
                                new Floor(leg, std::vector<Rate>(1, strike)));
                break;
              default:
                QL_FAIL("unknown cap/floor type");
            }
            result->setPricingEngine(makeEngine(volatility));
            return result;
        }

    };

    bool checkAbsError(Real x1, Real x2, Real tolerance){
        return std::fabs(x1 - x2) < tolerance;
    }

    std::string typeToString(CapFloor::Type type) {
        switch (type) {
          case CapFloor::Cap:
            return "cap";
          case CapFloor::Floor:
            return "floor";
          case CapFloor::Collar:
            return "collar";
          default:
            QL_FAIL("unknown cap/floor type");
        }
    }

}


void CapFloorTest::testVega() {

    BOOST_TEST_MESSAGE("Testing cap/floor vega...");

    CommonVars vars;

    Integer lengths[] = { 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 30 };
    Volatility vols[] = { 0.01, 0.05, 0.10, 0.15, 0.20 };
    Rate strikes[] = { 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09 };
    CapFloor::Type types[] = { CapFloor::Cap, CapFloor::Floor};

    Date startDate = vars.termStructure->referenceDate();
    static const Real shift = 1e-8;
    static const Real tolerance = 0.005;

    for (int length : lengths) {
        for (Size j=0; j<LENGTH(vols); j++) {
            for (double strike : strikes) {
                for (auto & type : types) {
                    Leg leg = vars.makeLeg(startDate, length);
                    ext::shared_ptr<CapFloor> capFloor =
                        vars.makeCapFloor(type,leg,
                                          strike,vols[j]);
                    ext::shared_ptr<CapFloor> shiftedCapFloor2 =
                        vars.makeCapFloor(type,leg,
                                          strike,vols[j]+shift);
                     ext::shared_ptr<CapFloor> shiftedCapFloor1 =
                        vars.makeCapFloor(type,leg,
                                          strike,vols[j]-shift);
                    Real value1 = shiftedCapFloor1->NPV();
                    Real value2 = shiftedCapFloor2->NPV();
                    Real numericalVega = (value2 - value1) / (2*shift);
                    if (numericalVega>1.0e-4) {
                        Real analyticalVega = capFloor->result<Real>("vega");
                        Real discrepancy =
                            std::fabs(numericalVega - analyticalVega);
                        discrepancy /= numericalVega;
                        if (discrepancy > tolerance)
                            BOOST_FAIL(
                                "failed to compute cap/floor vega:" <<
                                "\n   lengths:     " << lengths[j]*Years <<
                                "\n   strike:      " << io::rate(strike) <<
                                //"\n   types:       " << types[h] <<
                                std::fixed << std::setprecision(12) <<
                                "\n   calculated:  " << analyticalVega <<
                                "\n   expected:    " << numericalVega <<
                                "\n   discrepancy: " << io::rate(discrepancy) <<
                                "\n   tolerance:   " << io::rate(tolerance));
                     }
                }
            }
        }
    }
}

void CapFloorTest::testStrikeDependency() {

    BOOST_TEST_MESSAGE("Testing cap/floor dependency on strike...");

    CommonVars vars;

    Integer lengths[] = { 1, 2, 3, 5, 7, 10, 15, 20 };
    Volatility vols[] = { 0.01, 0.05, 0.10, 0.15, 0.20 };
    Rate strikes[] = { 0.03, 0.04, 0.05, 0.06, 0.07 };

    Date startDate = vars.termStructure->referenceDate();

    for (int length : lengths) {
        for (double vol : vols) {
            // store the results for different strikes...
            std::vector<Real> cap_values, floor_values;
            for (double strike : strikes) {
                Leg leg = vars.makeLeg(startDate,length);
                ext::shared_ptr<Instrument> cap =
                    vars.makeCapFloor(CapFloor::Cap,leg,
                                      strike,vol);
                cap_values.push_back(cap->NPV());
                ext::shared_ptr<Instrument> floor =
                    vars.makeCapFloor(CapFloor::Floor,leg,
                                      strike,vol);
                floor_values.push_back(floor->NPV());
            }
            // and check that they go the right way
            auto it =
                std::adjacent_find(cap_values.begin(),cap_values.end(),
                                   std::less<Real>());
            if (it != cap_values.end()) {
                Size n = it - cap_values.begin();
                BOOST_FAIL(
                    "NPV is increasing with the strike in a cap: \n"
                    << std::setprecision(2)
                    << "    length:     " << length << " years\n"
                    << "    volatility: " << io::volatility(vol) << "\n"
                    << "    value:      " << cap_values[n]
                    << " at strike: " << io::rate(strikes[n]) << "\n"
                    << "    value:      " << cap_values[n+1]
                    << " at strike: " << io::rate(strikes[n+1]));
            }
            // same for floors
            it = std::adjacent_find(floor_values.begin(),floor_values.end(),
                                    std::greater<Real>());
            if (it != floor_values.end()) {
                Size n = it - floor_values.begin();
                BOOST_FAIL(
                    "NPV is decreasing with the strike in a floor: \n"
                    << std::setprecision(2)
                    << "    length:     " << length << " years\n"
                    << "    volatility: " << io::volatility(vol) << "\n"
                    << "    value:      " << floor_values[n]
                    << " at strike: " << io::rate(strikes[n]) << "\n"
                    << "    value:      " << floor_values[n+1]
                    << " at strike: " << io::rate(strikes[n+1]));
            }
        }
    }
}

void CapFloorTest::testConsistency() {

    BOOST_TEST_MESSAGE("Testing consistency between cap, floor and collar...");

    CommonVars vars;

    Integer lengths[] = { 1, 2, 3, 5, 7, 10, 15, 20 };
    Rate cap_rates[] = { 0.03, 0.04, 0.05, 0.06, 0.07 };
    Rate floor_rates[] = { 0.03, 0.04, 0.05, 0.06, 0.07 };
    Volatility vols[] = { 0.01, 0.05, 0.10, 0.15, 0.20 };

    Date startDate = vars.termStructure->referenceDate();

    for (int length : lengths) {
      for (Size j=0; j<LENGTH(cap_rates); j++) {
        for (double floor_rate : floor_rates) {
          for (double vol : vols) {

              Leg leg = vars.makeLeg(startDate,length);
              ext::shared_ptr<CapFloor> cap =
                  vars.makeCapFloor(CapFloor::Cap,leg,
                                    cap_rates[j],vol);
              ext::shared_ptr<CapFloor> floor =
                  vars.makeCapFloor(CapFloor::Floor,leg,
                                    floor_rate,vol);
              Collar collar(leg,std::vector<Rate>(1,cap_rates[j]),
                            std::vector<Rate>(1,floor_rate));
              collar.setPricingEngine(vars.makeEngine(vol));

              if (std::fabs((cap->NPV()-floor->NPV())-collar.NPV()) > 1e-10) {
                  BOOST_FAIL(
                    "inconsistency between cap, floor and collar:\n"
                    << "    length:       " << length << " years\n"
                    << "    volatility:   " << io::volatility(vol) << "\n"
                    << "    cap value:    " << cap->NPV()
                    << " at strike: " << io::rate(cap_rates[j]) << "\n"
                    << "    floor value:  " << floor->NPV()
                    << " at strike: " << io::rate(floor_rate) << "\n"
                    << "    collar value: " << collar.NPV());


              // test re-composition by optionlets, N.B. two per year
              Real capletsNPV = 0.0;
              std::vector<ext::shared_ptr<CapFloor> > caplets;
              for (Integer m=0; m<length*2; m++) {
                caplets.push_back(cap->optionlet(m));
                caplets[m]->setPricingEngine(vars.makeEngine(vol));
                capletsNPV += caplets[m]->NPV();
              }

              if (std::fabs(cap->NPV() - capletsNPV) > 1e-10) {
                BOOST_FAIL(
                  "sum of caplet NPVs does not equal cap NPV:\n"
                    << "    length:       " << length << " years\n"
                    << "    volatility:   " << io::volatility(vol) << "\n"
                    << "    cap value:    " << cap->NPV()
                    << " at strike: " << io::rate(cap_rates[j]) << "\n"
                    << "    sum of caplets value:  " << capletsNPV
                    << " at strike (first): " << io::rate(caplets[0]->capRates()[0]) << "\n"
                );
              }

              Real floorletsNPV = 0.0;
              std::vector<ext::shared_ptr<CapFloor> > floorlets;
              for (Integer m=0; m<length*2; m++) {
                floorlets.push_back(floor->optionlet(m));
                floorlets[m]->setPricingEngine(vars.makeEngine(vol));
                floorletsNPV += floorlets[m]->NPV();
              }

              if (std::fabs(floor->NPV() - floorletsNPV) > 1e-10) {
                BOOST_FAIL(
                  "sum of floorlet NPVs does not equal floor NPV:\n"
                    << "    length:       " << length << " years\n"
                    << "    volatility:   " << io::volatility(vol) << "\n"
                    << "    cap value:    " << floor->NPV()
                    << " at strike: " << io::rate(floor_rates[j]) << "\n"
                    << "    sum of floorlets value:  " << floorletsNPV
                    << " at strike (first): " << io::rate(floorlets[0]->floorRates()[0]) << "\n"
                );
              }

              Real collarletsNPV = 0.0;
              std::vector<ext::shared_ptr<CapFloor> > collarlets;
              for (Integer m=0; m<length*2; m++) {
                collarlets.push_back(collar.optionlet(m));
                collarlets[m]->setPricingEngine(vars.makeEngine(vol));
                collarletsNPV += collarlets[m]->NPV();
              }

              if (std::fabs(collar.NPV() - collarletsNPV) > 1e-10) {
                BOOST_FAIL(
                  "sum of collarlet NPVs does not equal floor NPV:\n"
                    << "    length:       " << length << " years\n"
                    << "    volatility:   " << io::volatility(vol) << "\n"
                    << "    cap value:    " << collar.NPV()
                    << " at strike floor: " << io::rate(floor_rates[j])
                    << " at strike cap: " << io::rate(cap_rates[j]) << "\n"
                    << "    sum of collarlets value:  " << collarletsNPV
                    << " at strike floor (first): " << io::rate(collarlets[0]->floorRates()[0])
                    << " at strike cap (first): " << io::rate(collarlets[0]->capRates()[0]) << "\n"
                );
              }




              }
          }
        }
      }
    }
}

void CapFloorTest::testParity() {

    BOOST_TEST_MESSAGE("Testing cap/floor parity...");

    CommonVars vars;

    Integer lengths[] = { 1, 2, 3, 5, 7, 10, 15, 20 };
    Rate strikes[] = { 0., 0.03, 0.04, 0.05, 0.06, 0.07 };
    Volatility vols[] = { 0.01, 0.05, 0.10, 0.15, 0.20 };

    Date startDate = vars.termStructure->referenceDate();

    for (int length : lengths) {
      for (double strike : strikes) {
        for (double vol : vols) {

            Leg leg = vars.makeLeg(startDate,length);
            ext::shared_ptr<Instrument> cap =
                vars.makeCapFloor(CapFloor::Cap,leg,
                                  strike,vol);
            ext::shared_ptr<Instrument> floor =
                vars.makeCapFloor(CapFloor::Floor,leg,
                                  strike,vol);
            Date maturity = vars.calendar.advance(startDate,length,Years,
                                              vars.convention);
            Schedule schedule(startDate,maturity,
                              Period(vars.frequency),vars.calendar,
                              vars.convention,vars.convention,
                              DateGeneration::Forward,false);
            VanillaSwap swap(VanillaSwap::Payer, vars.nominals[0],
                             schedule, strike, vars.index->dayCounter(),
                             schedule, vars.index, 0.0,
                             vars.index->dayCounter());
            swap.setPricingEngine(ext::shared_ptr<PricingEngine>(
                              new DiscountingSwapEngine(vars.termStructure)));
            // FLOATING_POINT_EXCEPTION
            if (std::fabs((cap->NPV()-floor->NPV()) - swap.NPV()) > 1.0e-10) {
                BOOST_FAIL(
                    "put/call parity violated:\n"
                    << "    length:      " << length << " years\n"
                    << "    volatility:  " << io::volatility(vol) << "\n"
                    << "    strike:      " << io::rate(strike) << "\n"
                    << "    cap value:   " << cap->NPV() << "\n"
                    << "    floor value: " << floor->NPV() << "\n"
                    << "    swap value:  " << swap.NPV());
            }
        }
      }
    }
}

void CapFloorTest::testATMRate() {

    BOOST_TEST_MESSAGE("Testing cap/floor ATM rate...");

    CommonVars vars;

    Integer lengths[] = { 1, 2, 3, 5, 7, 10, 15, 20 };
    Rate strikes[] = { 0., 0.03, 0.04, 0.05, 0.06, 0.07 };
    Volatility vols[] = { 0.01, 0.05, 0.10, 0.15, 0.20 };

    Date startDate = vars.termStructure->referenceDate();

    for (int length : lengths) {
        Leg leg = vars.makeLeg(startDate,length);
        Date maturity = vars.calendar.advance(startDate,length,Years,
                                  vars.convention);
        Schedule schedule(startDate,maturity,
                          Period(vars.frequency),vars.calendar,
                          vars.convention,vars.convention,
                          DateGeneration::Forward,false);

        for (double strike : strikes) {
            for (double vol : vols) {
                ext::shared_ptr<CapFloor> cap =
                    vars.makeCapFloor(CapFloor::Cap, leg, strike,vol);
                ext::shared_ptr<CapFloor> floor =
                    vars.makeCapFloor(CapFloor::Floor, leg, strike,vol);
                Rate capATMRate = cap->atmRate(**vars.termStructure);
                Rate floorATMRate = floor->atmRate(**vars.termStructure);
                if (!checkAbsError(floorATMRate, capATMRate, 1.0e-10))
                    BOOST_FAIL(
                      "Cap ATM Rate and floor ATM Rate should be equal :\n"
                      << "   length:        " << length << " years\n"
                      << "   volatility:    " << io::volatility(vol) << "\n"
                      << "   strike:        " << io::rate(strike) << "\n"
                      << "   cap ATM rate:  " << capATMRate << "\n"
                      << "   floor ATM rate:" << floorATMRate << "\n"
                      << "   relative Error:"
                      << relativeError(capATMRate, floorATMRate,
                                       capATMRate)*100 << "%" );
                VanillaSwap swap(VanillaSwap::Payer, vars.nominals[0],
                                 schedule, floorATMRate,
                                 vars.index->dayCounter(),
                                 schedule, vars.index, 0.0,
                                 vars.index->dayCounter());
                swap.setPricingEngine(ext::shared_ptr<PricingEngine>(
                              new DiscountingSwapEngine(vars.termStructure)));
                Real swapNPV = swap.NPV();
                if (!checkAbsError(swapNPV, 0, 1.0e-10))
                    BOOST_FAIL(
                      "the NPV of a Swap struck at ATM rate "
                      "should be equal to 0:\n"
                      << "   length:        " << length << " years\n"
                      << "   volatility:    " << io::volatility(vol) << "\n"
                      << "   ATM rate:      " << io::rate(floorATMRate) << "\n"
                      << "   swap NPV:      " << swapNPV);
        }
      }
    }
}


void CapFloorTest::testImpliedVolatility() {

    BOOST_TEST_MESSAGE("Testing implied term volatility for cap and floor...");

    CommonVars vars;

    Size maxEvaluations = 100;
    Real tolerance = 1.0e-8;

    CapFloor::Type types[] = { CapFloor::Cap, CapFloor::Floor };
    Rate strikes[] = { 0.02, 0.03, 0.04 };
    Integer lengths[] = { 1, 5, 10 };

    // test data
    Rate rRates[] = { 0.02, 0.03, 0.04, 0.05, 0.06, 0.07 };
    Volatility vols[] = { 0.01, 0.05, 0.10, 0.20, 0.30, 0.70, 0.90 };

    for (int length : lengths) {
        Leg leg = vars.makeLeg(vars.settlement, length);

        for (auto & type : types) {
            for (double strike : strikes) {

                ext::shared_ptr<CapFloor> capfloor =
                    vars.makeCapFloor(type, leg, strike, 0.0);

                for (double r : rRates) {
                    for (double v : vols) {

                        vars.termStructure.linkTo(flatRate(vars.settlement,r,
                                                       Actual360()));
                        capfloor->setPricingEngine(vars.makeEngine(v));

                        Real value = capfloor->NPV();
                        Volatility implVol = 0.0;
                        try {
                            implVol =
                                capfloor->impliedVolatility(value,
                                                            vars.termStructure,
                                                            0.10,
                                                            tolerance,
                                                            maxEvaluations,
                                                            10.0e-7, 4.0,
                                                            ShiftedLognormal, 0.0);
                        } catch (std::exception& e) {
                            // couldn't bracket?
                            capfloor->setPricingEngine(vars.makeEngine(0.0));
                            Real value2 = capfloor->NPV();
                            if (std::fabs(value-value2) < tolerance) {
                                // ok, just skip:
                                continue;
                            }
                            // otherwise, report error
                            BOOST_ERROR("implied vol failure: " <<
                                        typeToString(type) <<
                                        "\n  strike:     " << io::rate(strike) <<
                                        "\n  risk-free:  " << io::rate(r) <<
                                        "\n  length:     " << length << "Y" <<
                                        "\n  volatility: " << io::volatility(v) <<
                                        "\n  price:      " << value <<
                                        "\n" << e.what());
                        }
                        if (std::fabs(implVol-v) > tolerance) {
                            // the difference might not matter
                            capfloor->setPricingEngine(
                                                    vars.makeEngine(implVol));
                            Real value2 = capfloor->NPV();
                            if (std::fabs(value-value2) > tolerance) {
                            BOOST_FAIL("implied vol failure: " <<
                                       typeToString(type) <<
                                       "\n  strike:        " << io::rate(strike) <<
                                       "\n  risk-free:     " << io::rate(r) <<
                                       "\n  length:        " << length << "Y" <<
                                       "\n  volatility:    " << io::volatility(v) <<
                                       "\n  price:         " << value <<
                                       "\n  implied vol:   " << io::volatility(implVol) <<
                                       "\n  implied price: " << value2);
                            }
                        }
                    }
                }
            }
        }
    }
}

void CapFloorTest::testCachedValue() {

    BOOST_TEST_MESSAGE("Testing Black cap/floor price against cached values...");

    CommonVars vars;

    Date cachedToday(14,March,2002),
         cachedSettlement(18,March,2002);
    Settings::instance().evaluationDate() = cachedToday;
    vars.termStructure.linkTo(flatRate(cachedSettlement, 0.05, Actual360()));
    Date startDate = vars.termStructure->referenceDate();
    Leg leg = vars.makeLeg(startDate,20);
    ext::shared_ptr<Instrument> cap = vars.makeCapFloor(CapFloor::Cap,leg,
                                                          0.07,0.20);
    ext::shared_ptr<Instrument> floor = vars.makeCapFloor(CapFloor::Floor,leg,
                                                            0.03,0.20);
#ifndef QL_USE_INDEXED_COUPON
    // par coupon price
    Real cachedCapNPV   = 6.87570026732,
         cachedFloorNPV = 2.65812927959;
#else
    // index fixing price
    Real cachedCapNPV   = 6.87630307745,
         cachedFloorNPV = 2.65796764715;
#endif
    // test Black cap price against cached value
    if (std::fabs(cap->NPV()-cachedCapNPV) > 1.0e-11)
        BOOST_ERROR(
            "failed to reproduce cached cap value:\n"
            << std::setprecision(12)
            << "    calculated: " << cap->NPV() << "\n"
            << "    expected:   " << cachedCapNPV);
    // test Black floor price against cached value
    if (std::fabs(floor->NPV()-cachedFloorNPV) > 1.0e-11)
        BOOST_ERROR(
            "failed to reproduce cached floor value:\n"
            << std::setprecision(12)
            << "    calculated: " << floor->NPV() << "\n"
            << "    expected:   " << cachedFloorNPV);
}


test_suite* CapFloorTest::suite() {
    test_suite* suite = BOOST_TEST_SUITE("Cap and floor tests");
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testStrikeDependency));
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testConsistency));
    // FLOATING_POINT_EXCEPTION
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testParity));
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testVega));
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testATMRate));
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testImpliedVolatility));
    suite->add(QUANTLIB_TEST_CASE(&CapFloorTest::testCachedValue));
    return suite;
}

