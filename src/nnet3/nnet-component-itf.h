// nnet3/nnet-component-itf.h

// Copyright      2015  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#ifndef KALDI_NNET3_NNET_COMPONENT_ITF_H_
#define KALDI_NNET3_NNET_COMPONENT_ITF_H_

#include "nnet3/nnet-common.h"
#include "base/kaldi-error.h"
#include "thread/kaldi-mutex.h"
#include <iostream>

namespace kaldi {
namespace nnet3 {

// enum used to store various binary component properties.
// We give it a name ComponentProperties, but don't use this
// type for the bitmasks: instead use int32 for this type, e.g.
// int32 properties = kSimpleComponent|kBackpropNeedsOutput.
enum ComponentProperties {

  kSimpleComponent = 0x001,  // true if number of rows of input equals number of rows
                             // of output and this component doesn't care about the indexes
                             // (i.e. it maps each row of input to each row of output without
                             // regard to the index values).  Will normally be true.
  kUpdatableComponent = 0x002,  // true if the component has parameters that can be updated.
  kLinearInInput = 0x004,    // true if the component's output is always a
                             // linear function of its input, i.e. alpha times
                             // input gives you alpha times output.
  kLinearInParameters = 0x008, // true if an updatable component's output is always a
                               // linear function of its parameters, i.e. alpha times
                               // parameters gives you alpha times output.  This is true
                               // for all updatable components we envisage.
  kBackpropNeedsInput  = 0x010,  // true if backprop operation needs access to
                                 // forward-pass input.
  kBackpropNeedsOutput = 0x020,  // true if backprop operation needs access to
                                 // forward-pass output (e.g. true for Sigmoid).
  kPropagateInPlace = 0x040,  // true if we can do the propagate operation in-place
                              // (input and output matrices are the same).
                              // Note: if doing backprop, you'd also need to check
                              // that the kBackpropNeedsInput property is not true.
  kBackpropInPlace = 0x080,   // true if we can do the backprop operation in-place
                             // (input and output matrices may be the same).
  kPropagateAdds = 0x100,  // true if the Propagate function adds to, rather
                           // than setting, its output.  The Component chooses
                           // whether to add or set, and the calling code has to
                           // accommodate it.
  kBackpropAdds = 0x200,   // true if the Backprop function adds to, rather than
                           // setting, its output.  The Component chooses
                           // whether to add or set, and the calling code has to
                           // accommodate it.
};


// This is a base class for a helper-class of class Component, which is used to
// store any pre-computed indexes it needs for its forward and backward
// computations.  For components which are not "Simple" components (i.e. the
// kSimpleComponent property is false), and which may therefore "care" about
// which index the input and output matrix's rows represent (i.e. about
// which "struct Index" each row corresponds to), their CreateIndexes() function
// will be called prior to Propagate() and Backprop(), to create an object which
// must be a child class of class ComponentPrecomputedIndexes, where they
// can store any indexes that they need.
class ComponentPrecomputedIndexes {
 public:
  virtual ~ComponentPrecomputedIndexes();
};


/// Abstract base-class for neural-net components.
class Component {
 public:
  /// \brief Propagate function.
  ///   \param [in] indexes  A pointer to some information output by this class's
  ///      PrecomputeIndexes function (will be NULL for simple components,
  ///      i.e. those that don't do things like splicing).
  ///   \param [in] in   The input to this component.  Num-columns == InputDim().
  ///   \param [out] out  The output of this component.  Num-columns == OutputDim().
  ///      Note: output of this component will be added to the initial value of
  ///      "out" if Properties()&kPropagateAdds != 0; otherwise the output will
  ///      be set and the initial value ignored.  Each Component chooses whether
  ///      it is more convenient implementation-wise to add or set, and the
  ///      calling code has to deal with it.
  virtual void Propagate(const ComponentPrecomputedIndexes *indexes,
                         const CuMatrixBase<BaseFloat> &in,
                         CuMatrixBase<BaseFloat> *out) const = 0;

  /// \brief Backprop function.
  ///   \param [in] debug_info  Some kind of component name and/or index in
  ///     the network, to be printed out in any warning messages so we can
  ///     identify which layer the message pertains so.
  ///   \param [in] indexes     A pointer to some information output by this
  ///      class's PrecomputeIndexes function (will be NULL for simple
  ///      components, i.e. those that don't do things like splicing).
  ///   \param [in] in_value    The matrix that was given as input to the
  ///      Propagate function.  Will be ignored (and may be empty) if
  ///      Properties()&kBackpropNeedsInput == 0.
  ///   \param [in] out_value   The matrix that was output from the Propagate
  ///      function.  Will be ignored (and may be empty) if
  ///      Properties()&kBackpropNeedsOutput == 0
  ///   \param [in] out_deriv  The derivative at the output of this component.
  ///   \param [out] to_update  If model update is desired, the Component
  ///       to be updated, else NULL.  Does not have to be idential to this.
  ///   \param [out] in_deriv   The derivative at the input of this component,
  ///       if needed (else NULL).   If  Properties()&kBackpropInPlace, may be
  ///       the same matrix as out_deriv.
  virtual void Backprop(const std::string &debug_info,
                        const ComponentPrecomputedIndexes *indexes,
                        const CuMatrixBase<BaseFloat> &in_value,
                        const CuMatrixBase<BaseFloat> &out_value,                        
                        const CuMatrixBase<BaseFloat> &out_deriv,
                        Component *to_update, // may be NULL; may be identical
                                              // to "this" or different.
                        CuMatrixBase<BaseFloat> *in_deriv) const = 0;

  /// \brief  For a given index at the output of the component, tells us what indexes
  ///   are required at its input.
  /// \param [in] misc_info  This argument is supplied to handle things that the
  ///       framework can't very easily supply: information like which time
  ///       indexes are needed for AggregateComponent, which time-indexes are
  ///       available at the input of a recurrent network, and so on.  We will
  ///       add members to misc_info as needed.
  /// \param [in] output_index  The Index at the output of the component, for
  ///       which we are requesting the list of indexes at the component's input.
  /// \param [out] input_indexes  A list of indexes required at the input.
  ///
  /// The default implementation of this function is suitable for any
  /// SimpleComponent; it just copies the output_index to a single identical
  /// input_index).
  virtual void GetInputIndexes(const MiscComputationInfo &misc_info,
                               const Index &output_index,
                               std::vector<Index> *input_indexes) const;

  /// \brief (For non-simple Components) Returns some precomputed
  ///     component-specific and computation-specific indexes to be in used
  ///     in the Propagate and Backprop functions.
  /// \param [in] misc_info  This argument is supplied to handle things that the
  ///       framework can't very easily supply: information like which time
  ///       indexes are needed for AggregateComponent, which time-indexes are
  ///       available at the input of a recurrent network, and so on.  We will
  ///       add members to misc_info as needed.
  /// \param [in] input_indexes  A vector of indexes corresponding that explains
  ///       what time-indexes (and other indexes) each row of the
  ///       in/in_value/in_deriv matrices given to Propagate and Backprop will
  ///       mean.
  /// \param [in] output_indexes  A vector of indexes corresponding that explains
  ///       what time-indexes (and other indexes) each row of the
  ///       out/out_value/out_deriv matrices given to Propagate and Backprop will
  ///       mean.
  /// \return  Returns a child-class of class ComponentPrecomputedIndexes, or
  ///       NULL if this component for does not need to precompute any indexes
  ///       (e.g. if it is a simple component and does not care about indexes).
  virtual ComponentPrecomputedIndexes* PrecomputeIndexes(
      const MiscComputationInfo &misc_info,
      const std::vector<Index> &input_indexes,
      std::vector<Index> &output_indexes,
      bool need_backprop) const { return NULL;  }

  /// \brief Returns a string such as "SigmoidComponent", describing
  ///        the type of the object.
  virtual std::string Type() const = 0; 

  /// \brief  Initialize, typically from a line of a config file.
  /// \param [in] args  A string containing any parameters that need to be
  ///            For example: "dim=100 param-stddev=0.1"
  virtual void InitFromString(std::string args) = 0;
  
  /// \brief Returns input-dimension of this component.
  virtual int32 InputDim() const = 0;
  
  /// \brief Returns output-dimension of this component.
  virtual int32 OutputDim() const = 0;

  /// \brief Return bitmask of the component's properties.
  ///   These properties depend only on the component's type.
  ///   See enum ComponentProperties.
  virtual int32 Properties() const = 0;

  /// \brief Read component from stream (works out its type)
  static Component* ReadNew(std::istream &is, bool binary);

  /// \brief Copies component (deep copy).
  virtual Component* Copy() const = 0;  
  
  /// \brief Initialize the Component from one config-file line
  /// \param [in] initializer_line  Typically something like
  ///      "AffineComponent input-dim=1000 output-dim=1000"
  /// \return Returns newly created Component.
  static Component *NewFromString(const std::string &initializer_line);

  /// \brief Returns a new Component of the given type e.g. "SoftmaxComponent",
  ///   or NULL if no such component type exists. 
  static Component *NewComponentOfType(const std::string &type);

  /// \brief Read function (used after we know the type of the Component);
  ///   accepts input that is missing the token that describes the component
  ///   type, in case it has already been consumed.
  virtual void Read(std::istream &is, bool binary) = 0; 
  
  /// \brief Write component to stream
  virtual void Write(std::ostream &os, bool binary) const = 0;

  /// \brief Returns some text-form information about this component, for diagnostics.
  virtual std::string Info() const;

  Component() { }
  
  virtual ~Component() { }

 private:  
  KALDI_DISALLOW_COPY_AND_ASSIGN(Component);
};



/**
 * Class UpdatableComponent is a Component which has trainable parameters; it
 * extends the interface of Component.  This is a base-class for Components with
 * parameters.
 */
class UpdatableComponent: public Component {
 public:
  UpdatableComponent(const UpdatableComponent &other):
      learning_rate_(other.learning_rate_),
      is_gradient_(other.is_gradient_) { }
  
  void Init(BaseFloat lr, bool is_gradient = false);

  UpdatableComponent(BaseFloat learning_rate) {  Init(learning_rate); }

  /// \brief Sets parameters to zero, and if treat_as_gradient is true,
  ///    sets is_gradient_ to true and the learning rate to 1.
  virtual void SetZero(bool treat_as_gradient) = 0;
  
  UpdatableComponent(): learning_rate_(0.001) { }
  
  virtual ~UpdatableComponent() { }

  /// \brief Computes dot-product between parameters of two instances of a
  ///  Component.
  virtual BaseFloat DotProduct(const UpdatableComponent &other) const = 0;
  
  /// This function is to be used in testing.
  virtual void PerturbParams(BaseFloat stddev) = 0;
  
  /// This virtual function (not in base-class Component) scales the parameters
  /// by "scale".
  virtual void Scale(BaseFloat scale) = 0;

  /// This virtual function (not in base-class Component) adds the parameters of
  /// another updatable component, times some constant, to the current
  /// parameters.
  virtual void Add(BaseFloat alpha, const UpdatableComponent &other) = 0;
  
  /// Sets the learning rate of gradient descent
  void SetLearningRate(BaseFloat lrate) {  learning_rate_ = lrate; }

  /// Gets the learning rate of gradient descent
  BaseFloat LearningRate() const { return learning_rate_; }

  virtual std::string Info() const;
  
  /// The following new virtual function returns the total dimension of
  /// the parameters in this class.
  virtual int32 GetParameterDim() const { KALDI_ASSERT(0); return 0; }

  /// Turns the parameters into vector form.  We put the vector form on the CPU,
  /// because in the kinds of situations where we do this, we'll tend to use
  /// too much memory for the GPU.
  virtual void Vectorize(VectorBase<BaseFloat> *params) const { KALDI_ASSERT(0); }
  /// Converts the parameters from vector form.
  virtual void UnVectorize(const VectorBase<BaseFloat> &params) {
    KALDI_ASSERT(0);
  }
  
 protected: 
  BaseFloat learning_rate_; ///< learning rate (typically 0.0..0.01)
  bool is_gradient_;  ///< True if this component is to be treated as a gradient rather
                      ///< than as parameters.  Its main effect is that we disable
                      ///< any natural-gradient update and just compute the standard
                      ///< gradient.
 private:
  const UpdatableComponent &operator = (const UpdatableComponent &other); // Disallow.
};

/// This kind of Component is a base-class for things like sigmoid, softmax and
/// ReLU: nonlinearities that don't change the dimension.  It takes care of
/// storing statistics on the average activations and derivatives encountered
/// during training.
class NonlinearComponent: public Component {
 public:
  void Init(int32 dim) { dim_ = dim; count_ = 0.0; }
  explicit NonlinearComponent(int32 dim) { Init(dim); }
  NonlinearComponent(): dim_(0) { } // e.g. prior to Read().
  explicit NonlinearComponent(const NonlinearComponent &other);
  
  virtual int32 InputDim() const { return dim_; }
  virtual int32 OutputDim() const { return dim_; }
  
  /// We implement InitFromString at this level.
  virtual void InitFromString(std::string args);
  
  /// We implement Read at this level as it just needs the Type().
  virtual void Read(std::istream &is, bool binary);
  
  /// Write component to stream.
  virtual void Write(std::ostream &os, bool binary) const;

  // relates to scaling activationstats, not parameters.
  void Scale(BaseFloat scale);

  // relates to adding stats  
  void Add(BaseFloat alpha, const NonlinearComponent &other);

  // The following functions are unique to NonlinearComponent.
  // They mostly relate to diagnostics.
  const CuVector<double> &ValueSum() const { return value_sum_; }
  const CuVector<double> &DerivSum() const { return deriv_sum_; }
  
  double Count() const { return count_; }

 protected:
  friend class NormalizationComponent;
  friend class SigmoidComponent;
  friend class TanhComponent;
  friend class SoftmaxComponent;
  friend class RectifiedLinearComponent;
  
  // This function updates the stats "value_sum_", "deriv_sum_", and
  // count_. (If deriv == NULL, it won't update "deriv_sum_").
  // It will be called from the Backprop function of child classes.
  void UpdateStats(const CuMatrixBase<BaseFloat> &out_value,
                   const CuMatrixBase<BaseFloat> *deriv = NULL);

  
  const NonlinearComponent &operator = (const NonlinearComponent &other); // Disallow.
  int32 dim_;
  CuVector<double> value_sum_; // stats at the output.
  CuVector<double> deriv_sum_; // stats of the derivative of the nonlinearity
                               // (only applicable to element-by-element
                               // nonlinearities, not Softmax.
  double count_;
  // The mutex is used in UpdateStats, only for resizing vectors.
  Mutex mutex_;
};

} // namespace nnet3
} // namespace kaldi


#endif