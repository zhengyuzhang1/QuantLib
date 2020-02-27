/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2019 Klaus Spanderen

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

/*! \file fdsabrvanillaengine.hpp */

#include <ql/exercise.hpp>
#include <ql/termstructures/volatility/sabr.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/pricingengines/vanilla/fdsabrvanillaengine.hpp>
#include <ql/methods/finitedifferences/solvers/fdm2dimsolver.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/meshers/fdmcev1dmesher.hpp>
#include <ql/methods/finitedifferences/meshers/concentrating1dmesher.hpp>
#include <ql/methods/finitedifferences/operators/fdmsabrop.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/utilities/fdmdiscountdirichletboundary.hpp>

#include <ql/methods/finitedifferences/utilities/cevrndcalculator.hpp>

namespace QuantLib {

    FdSabrVanillaEngine::FdSabrVanillaEngine(
        Real f0, Real alpha, Real beta, Real nu, Real rho,
        const Handle<YieldTermStructure>& rTS,
        Size tGrid, Size fGrid, Size xGrid,
        Size dampingSteps, Real scalingFactor, Real eps,
        const FdmSchemeDesc& schemeDesc)
    : f0_(f0), alpha_(alpha), beta_(beta), nu_(nu), rho_(rho),
      rTS_(rTS),
      tGrid_(tGrid),
      fGrid_(fGrid),
      xGrid_(xGrid),
      dampingSteps_(dampingSteps),
      scalingFactor_(scalingFactor),
      eps_(eps),
      schemeDesc_(schemeDesc) {

        validateSabrParameters(alpha, 0.5, nu, rho);

        QL_REQUIRE(beta<1.0, "beta must be smaller than 1.0: "
                   << beta << " not allowed");

        registerWith(rTS_);
    }

    void FdSabrVanillaEngine::calculate() const {
        // 1. Mesher
        const std::shared_ptr<StrikedTypePayoff> payoff =
            std::dynamic_pointer_cast<StrikedTypePayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-striked payoff given");

        const DayCounter dc = rTS_->dayCounter();

        const Date referenceDate = rTS_->referenceDate();
        const Date maturityDate = arguments_.exercise->lastDate();
        const Time maturityTime = dc.yearFraction(referenceDate, maturityDate);

        const Real upperAlpha = alpha_*
            std::exp(nu_*std::sqrt(maturityTime)*InverseCumulativeNormal()(0.75));

        const std::shared_ptr<Fdm1dMesher> cevMesher =
            std::make_shared<FdmCEV1dMesher>(
                fGrid_, f0_, upperAlpha, beta_,
                maturityTime, eps_, scalingFactor_,
                std::make_pair(payoff->strike(), 0.025));

        const Real normInvEps = InverseCumulativeNormal()(1-eps_);
        const Real logDrift = -0.5*nu_*nu_*maturityTime;
        const Real volRange =
            nu_*std::sqrt(maturityTime)*normInvEps*scalingFactor_;

        const Real xMin = std::log(alpha_) + logDrift - volRange;
        const Real xMax = std::log(alpha_) + logDrift + volRange;

        const std::shared_ptr<Fdm1dMesher> xMesher =
            std::make_shared<Concentrating1dMesher>(
                xMin, xMax, xGrid_, std::make_pair(std::log(alpha_), 0.1));

        const std::shared_ptr<FdmMesher> mesher =
           std::make_shared<FdmMesherComposite>(cevMesher, xMesher);

        // 2. Calculator
        const std::shared_ptr<FdmInnerValueCalculator> calculator =
            std::make_shared<FdmCellAveragingInnerValue>(payoff, mesher, 0);

        // 3. Step conditions
        const std::shared_ptr<FdmStepConditionComposite> conditions =
            FdmStepConditionComposite::vanillaComposite(
                DividendSchedule(), arguments_.exercise,
                mesher, calculator, referenceDate, dc);

        // 4. Boundary conditions
        FdmBoundaryConditionSet boundaries;

        const Real lowerBound = cevMesher->locations().front();
        const Real upperBound = cevMesher->locations().back();

        boundaries.push_back(
            std::make_shared<FdmDiscountDirichletBoundary>(
                mesher, rTS_.currentLink(),
                maturityTime, (*payoff)(upperBound),
                0, FdmDiscountDirichletBoundary::Upper));

        boundaries.push_back(
            std::make_shared<FdmDiscountDirichletBoundary>(
                mesher, rTS_.currentLink(),
                maturityTime, (*payoff)(lowerBound),
                0, FdmDiscountDirichletBoundary::Lower));

        // 5. Solver
        const FdmSolverDesc solverDesc = {
            mesher, boundaries, conditions,
            calculator, maturityTime, tGrid_, dampingSteps_
        };

        const std::shared_ptr<FdmLinearOpComposite> op =
            std::make_shared<FdmSabrOp>(
               mesher, rTS_.currentLink(),
               f0_, alpha_, beta_, nu_, rho_);

        const std::shared_ptr<Fdm2DimSolver> solver =
            std::make_shared<Fdm2DimSolver>(solverDesc, schemeDesc_, op);

        results_.value = solver->interpolateAt(f0_, std::log(alpha_));
    }
}

