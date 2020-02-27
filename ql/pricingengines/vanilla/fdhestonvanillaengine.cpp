/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008 Andreas Gaida
 Copyright (C) 2008, 2009 Ralph Schreyer
 Copyright (C) 2008, 2009, 2015 Klaus Spanderen
 Copyright (C) 2015 Johannes Goettker-Schnetmann

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


#include <ql/processes/batesprocess.hpp>
#include <ql/pricingengines/vanilla/fdhestonvanillaengine.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/solvers/fdmhestonsolver.hpp>
#include <ql/methods/finitedifferences/meshers/fdmhestonvariancemesher.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/meshers/fdmblackscholesmesher.hpp>
#include <ql/methods/finitedifferences/meshers/fdmblackscholesmultistrikemesher.hpp>
#include <utility>

namespace QuantLib {

    FdHestonVanillaEngine::FdHestonVanillaEngine(
            const std::shared_ptr<HestonModel>& model,
            Size tGrid, Size xGrid, Size vGrid, Size dampingSteps,
            const FdmSchemeDesc& schemeDesc,
            std::shared_ptr<LocalVolTermStructure> leverageFct)
    : GenericModelEngine<HestonModel,
                        DividendVanillaOption::arguments,
                        DividendVanillaOption::results>(model),
      tGrid_(tGrid), xGrid_(xGrid), 
      vGrid_(vGrid), dampingSteps_(dampingSteps),
      schemeDesc_(schemeDesc),
      leverageFct_(std::move(leverageFct)),
      quantoHelper_(std::shared_ptr<FdmQuantoHelper>()) {
    }

    FdHestonVanillaEngine::FdHestonVanillaEngine(
            const std::shared_ptr<HestonModel>& model,
            const std::shared_ptr<FdmQuantoHelper>& quantoHelper,
            Size tGrid, Size xGrid, Size vGrid, Size dampingSteps,
            const FdmSchemeDesc& schemeDesc,
            std::shared_ptr<LocalVolTermStructure> leverageFct)
    : GenericModelEngine<HestonModel,
                        DividendVanillaOption::arguments,
                        DividendVanillaOption::results>(model),
      tGrid_(tGrid), xGrid_(xGrid),
      vGrid_(vGrid), dampingSteps_(dampingSteps),
      schemeDesc_(schemeDesc),
      leverageFct_(leverageFct),
      quantoHelper_(quantoHelper) {
    }


    FdmSolverDesc FdHestonVanillaEngine::getSolverDesc(Real) const {
        // 1. Mesher
        const std::shared_ptr<HestonProcess> process = model_->process();
        const Time maturity = process->time(arguments_.exercise->lastDate());

        // 1.1 The variance mesher
        const Size tGridMin = 5;
        const Size tGridAvgSteps = std::max(tGridMin, tGrid_/50);
        const std::shared_ptr<FdmHestonLocalVolatilityVarianceMesher> vMesher
            = std::make_shared<FdmHestonLocalVolatilityVarianceMesher>(
                  vGrid_, process, leverageFct_, maturity, tGridAvgSteps);

        const Volatility avgVolaEstimate = vMesher->volaEstimate();

        // 1.2 The equity mesher
        const std::shared_ptr<StrikedTypePayoff> payoff =
            std::dynamic_pointer_cast<StrikedTypePayoff>(arguments_.payoff);

        std::shared_ptr<Fdm1dMesher> equityMesher;
        if (strikes_.empty()) {
            equityMesher = std::shared_ptr<Fdm1dMesher>(
                new FdmBlackScholesMesher(
                    xGrid_, 
                    FdmBlackScholesMesher::processHelper(
                        process->s0(), process->dividendYield(),
                        process->riskFreeRate(), avgVolaEstimate),
                    maturity, payoff->strike(),
                    Null<Real>(), Null<Real>(), 0.0001, 2.0,
                    std::pair<Real, Real>(payoff->strike(), 0.1),
                    arguments_.cashFlow,
                    quantoHelper_));
        }
        else {
            QL_REQUIRE(arguments_.cashFlow.empty(),"multiple strikes engine "
                       "does not work with discrete dividends");
            equityMesher = std::shared_ptr<Fdm1dMesher>(
                new FdmBlackScholesMultiStrikeMesher(
                    xGrid_,
                    FdmBlackScholesMesher::processHelper(
                      process->s0(), process->dividendYield(), 
                      process->riskFreeRate(), avgVolaEstimate),
                    maturity, strikes_, 0.0001, 1.5,
                    std::pair<Real, Real>(payoff->strike(), 0.075)));            
        }
        
        const std::shared_ptr<FdmMesher> mesher(
            new FdmMesherComposite(equityMesher, vMesher));

        // 2. Calculator
        const std::shared_ptr<FdmInnerValueCalculator> calculator(
                          new FdmLogInnerValue(arguments_.payoff, mesher, 0));

        // 3. Step conditions
        const std::shared_ptr<FdmStepConditionComposite> conditions = 
             FdmStepConditionComposite::vanillaComposite(
                                 arguments_.cashFlow, arguments_.exercise, 
                                 mesher, calculator, 
                                 process->riskFreeRate()->referenceDate(),
                                 process->riskFreeRate()->dayCounter());

        // 4. Boundary conditions
        const FdmBoundaryConditionSet boundaries;

        // 5. Solver
        FdmSolverDesc solverDesc = { mesher, boundaries, conditions,
                                     calculator, maturity,
                                     tGrid_, dampingSteps_ };

       return solverDesc;
    }

    void FdHestonVanillaEngine::calculate() const {

        // cache lookup for precalculated results
        for (auto & cachedArgs2result : cachedArgs2results_) {
            if (   cachedArgs2result.first.exercise->type()
                        == arguments_.exercise->type()
                && cachedArgs2result.first.exercise->dates()
                        == arguments_.exercise->dates()) {
                std::shared_ptr<PlainVanillaPayoff> p1 =
                    std::dynamic_pointer_cast<PlainVanillaPayoff>(
                                                            arguments_.payoff);
                std::shared_ptr<PlainVanillaPayoff> p2 =
                    std::dynamic_pointer_cast<PlainVanillaPayoff>(
                                          cachedArgs2result.first.payoff);

                if (p1 && p1->strike()     == p2->strike()
                       && p1->optionType() == p2->optionType()) {
                    QL_REQUIRE(arguments_.cashFlow.empty(),
                               "multiple strikes engine does "
                               "not work with discrete dividends");
                    results_ = cachedArgs2result.second;
                    return;
                }
            }
        }

        const std::shared_ptr<HestonProcess> process = model_->process();

        std::shared_ptr<FdmHestonSolver> solver(new FdmHestonSolver(
                    Handle<HestonProcess>(process),
                    getSolverDesc(1.5), schemeDesc_,
                    Handle<FdmQuantoHelper>(quantoHelper_), leverageFct_));

        const Real v0   = process->v0();
        const Real spot = process->s0()->value();

        results_.value = solver->valueAt(spot, v0);
        results_.delta = solver->deltaAt(spot, v0);
        results_.gamma = solver->gammaAt(spot, v0);
        results_.theta = solver->thetaAt(spot, v0);
        
        cachedArgs2results_.resize(strikes_.size());
        const std::shared_ptr<StrikedTypePayoff> payoff =
            std::dynamic_pointer_cast<StrikedTypePayoff>(arguments_.payoff);
        for (Size i=0; i < strikes_.size(); ++i) {
            cachedArgs2results_[i].first.exercise = arguments_.exercise;
            cachedArgs2results_[i].first.payoff = 
                std::make_shared<PlainVanillaPayoff>(
                    payoff->optionType(), strikes_[i]);
            const Real d = payoff->strike()/strikes_[i];
            
            DividendVanillaOption::results& 
                                results = cachedArgs2results_[i].second;
            results.value = solver->valueAt(spot*d, v0)/d;
            results.delta = solver->deltaAt(spot*d, v0);
            results.gamma = solver->gammaAt(spot*d, v0)*d;
            results.theta = solver->thetaAt(spot*d, v0)/d;                
        }
    }
    
    void FdHestonVanillaEngine::update() {
        cachedArgs2results_.clear();
        GenericModelEngine<HestonModel, DividendVanillaOption::arguments,
                           DividendVanillaOption::results>::update();
    }
    
    void FdHestonVanillaEngine::enableMultipleStrikesCaching(
                                        const std::vector<Real>& strikes) {
        strikes_ = strikes;
        cachedArgs2results_.clear();
    }


    MakeFdHestonVanillaEngine::MakeFdHestonVanillaEngine(
        const std::shared_ptr<HestonModel>& hestonModel)
      : hestonModel_(hestonModel),
        tGrid_(100),
        xGrid_(100),
        vGrid_(50),
        dampingSteps_(0),
        schemeDesc_(
            std::make_shared<FdmSchemeDesc>(FdmSchemeDesc::Hundsdorfer())),
        leverageFct_(std::shared_ptr<LocalVolTermStructure>()),
        quantoHelper_(std::shared_ptr<FdmQuantoHelper>()) {}

    MakeFdHestonVanillaEngine& MakeFdHestonVanillaEngine::withQuantoHelper(
        const std::shared_ptr<FdmQuantoHelper>& quantoHelper) {
        quantoHelper_ = quantoHelper;
        return *this;
    }

    MakeFdHestonVanillaEngine&
    MakeFdHestonVanillaEngine::withTGrid(Size tGrid) {
        tGrid_ = tGrid;
        return *this;
    }

    MakeFdHestonVanillaEngine&
    MakeFdHestonVanillaEngine::withXGrid(Size xGrid) {
        xGrid_ = xGrid;
        return *this;
    }

    MakeFdHestonVanillaEngine&
    MakeFdHestonVanillaEngine::withVGrid(Size vGrid) {
        vGrid_ = vGrid;
        return *this;
    }

    MakeFdHestonVanillaEngine&
    MakeFdHestonVanillaEngine::withDampingSteps(Size dampingSteps) {
        dampingSteps_ = dampingSteps;
        return *this;
    }

    MakeFdHestonVanillaEngine&
    MakeFdHestonVanillaEngine::withFdmSchemeDesc(
        const FdmSchemeDesc& schemeDesc) {
        schemeDesc_ = std::make_shared<FdmSchemeDesc>(schemeDesc);
        return *this;
    }

    MakeFdHestonVanillaEngine&
    MakeFdHestonVanillaEngine::withLeverageFunction(
        std::shared_ptr<LocalVolTermStructure>& leverageFct) {
        leverageFct_ = leverageFct;
        return *this;
    }

    MakeFdHestonVanillaEngine::operator
    std::shared_ptr<PricingEngine>() const {
        return std::make_shared<FdHestonVanillaEngine>(
            hestonModel_,
            quantoHelper_,
            tGrid_, xGrid_, vGrid_, dampingSteps_,
            *schemeDesc_,
            leverageFct_);
    }
}
