//
//  DropoutBridge_impl.hxx
//  moka
//
//  Created by Firas Abuzaid on 1/31/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#ifndef moka_DropoutBridge_impl_hxx
#define moka_DropoutBridge_impl_hxx

template <typename DataType, typename DriverClass>
DropoutBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::DropoutBridge(InputLayerType * const _p_input_layer,
    OutputLayerType * const _p_output_layer, const cnn::LayerParameter * const _layer_param,
    const cnn::SolverParameter * const _solver_param, DriverClass * const _p_driver)
: AbstractBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>(_p_input_layer, _p_output_layer,
    _layer_param, _solver_param, _p_driver), dropout_ratio(layer_param->dropout_param().dropout_ratio()) {

  report_forward_constructor.reset();
  report_forward_last_transfer.reset();
  report_forward_history.reset();
#ifdef _DO_ASSERT
  assert(oR == iR); assert(oC == iC);
  assert(oB == iB); assert(oD == iD);
#ifndef _SNAPSHOT
  assert(dropout_ratio > 0.);
#endif
  assert(dropout_ratio < 1.);
#endif

  scale = 1. / (1. - dropout_ratio);
  mask_cube = new LogicalCube<unsigned int, Layout_CRDB>(iR, iC, iD, iB);
  Util::bernoulli_initialize(mask_cube->get_p_data(), iR*iC*iD*iB, 1. - dropout_ratio);

  report_forward_constructor.end(0, 0, 0);
}

/**
 * Implements Dropout in the forward direction.
 **/
template <typename DataType, typename DriverClass>
void DropoutBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::forward() {
  // Copy input to device memory
  AbstractBridge<DataType, Layout_CRDB, DataType,Layout_CRDB, DriverClass>::copy_from_host_to_device(input_d_cube,
      p_input_layer->p_data_cube);
  // If DriverClass == CPUDriver, we also need to update the p_data pointer of output_d_cube to point to
  // p_output_layer->p_data_cube->p_data
  if (std::is_same<DriverClass, CPUDriver>::value) {
    AbstractBridge<DataType, Layout_CRDB, DataType,Layout_CRDB, DriverClass>::copy_from_host_to_device(
        output_d_cube, p_output_layer->p_data_cube
        );
  }

  report_forward_last_transfer.reset();
#ifdef _DO_ASSERT
  assert(p_input_layer->p_data_cube->n_elements == mask_cube->n_elements);
#endif

  ////////////////////////////////////////////////////////////////////////////////
  DeviceMemoryPointer * input = input_d_cube->get_device_pointer(p_driver);
  DeviceMemoryPointer * output = output_d_cube->get_device_pointer(p_driver);

  DeviceMemoryPointer * arg1 = p_driver->get_device_pointer(NULL, 0);

  // in the training phase, we apply the mask
  if (DeepNetConfig::train()) {
    _dropout_forward_train_arg_helper _arg;
    _arg.mask = (char *) mask_cube->get_p_data();
    _arg.scale = scale;

    DeviceMemoryPointer * arg2 = p_driver->get_device_pointer((void*)&_arg,
      sizeof(_dropout_forward_train_arg_helper));
    // SHADJIS TODO: Optimize for GPU
    p_driver->template parallel_map<_f_src_to_dst_dropout_forward,
      _f_dropout_forward_train>(input, output, sizeof(DataType), arg1, arg2);
  // in the testing phase, we simply copy from input to output
  } else {
    DeviceMemoryPointer * arg2 = p_driver->get_device_pointer(NULL, 0);
    // SHADJIS TODO: Optimize for GPU
    p_driver->template parallel_map<_f_src_to_dst_dropout_forward,
      _f_dropout_forward_test>(input, output, sizeof(DataType), arg1, arg2);
  }
  ////////////////////////////////////////////////////////////////////////////////

  // If DriverClass == GPUDriver (or DriverClass != CPUDriver), we copy output to host memory here
  if (!std::is_same<DriverClass, CPUDriver>::value) {
    // SHADJIS TODO:
	// ERROR: For now do not support this layer on device. Easy to fix though, just need to
	// make sure arg parameters which are pointers get copied to device
    assert(false);
    AbstractBridge<DataType, Layout_CRDB, DataType,Layout_CRDB, DriverClass>::copy_from_device_to_host(
        p_output_layer->p_data_cube, output_d_cube
        );
  }

  report_forward_last_transfer.end();
  report_forward_history.aggregate(report_forward_last_transfer);
}

/**
 * Implements Dropout in the backward direction.
 **/
template <typename DataType, typename DriverClass>
void DropoutBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::backward() {
  // Copy output grad to device memory
  AbstractBridge<DataType, Layout_CRDB, DataType,Layout_CRDB, DriverClass>::copy_from_host_to_device(output_g_cube,
      p_output_layer->p_gradient_cube);
  // If DriverClass == CPUDriver, we also need to update the p_data pointer of input_g_cube to point to
  // p_input_layer->p_gradient_cube->p_data
  if (std::is_same<DriverClass, CPUDriver>::value) {
    AbstractBridge<DataType, Layout_CRDB, DataType,Layout_CRDB, DriverClass>::copy_from_host_to_device(
        input_g_cube, p_input_layer->p_gradient_cube
        );
  }

  report_backward_updateweight_last_transfer.reset();

  ////////////////////////////////////////////////////////////////////////////////
  DeviceMemoryPointer * input = input_g_cube->get_device_pointer(p_driver);
  DeviceMemoryPointer * output = output_g_cube->get_device_pointer(p_driver);

  _dropout_forward_train_arg_helper _arg;
  _arg.mask = (char *) mask_cube->get_p_data();
  _arg.scale = scale;

  DeviceMemoryPointer * arg1 = p_driver->get_device_pointer(NULL, 0);
  DeviceMemoryPointer * arg2 = p_driver->get_device_pointer((void*)&_arg,
      sizeof(_dropout_forward_train_arg_helper));

  // the backward phase is the same as the forward phase, except we treat
  // the input gradient as the output, and the output gradient as the input
  // SHADJIS TODO: Optimize for GPU
  p_driver->template parallel_map<_f_src_to_dst_dropout_forward,
    _f_dropout_forward_train>(output, input, sizeof(DataType), arg1, arg2);
  ////////////////////////////////////////////////////////////////////////////////

  // If DriverClass == GPUDriver (or DriverClass != CPUDriver), we copy input grad to host memory here
  if (!std::is_same<DriverClass, CPUDriver>::value) {
    // SHADJIS TODO:
	// ERROR: For now do not support this layer on device. Easy to fix though, just need to
	// make sure arg parameters which are pointers get copied to device
    assert(false);
    AbstractBridge<DataType, Layout_CRDB, DataType,Layout_CRDB, DriverClass>::copy_from_device_to_host(
        p_input_layer->p_gradient_cube, input_g_cube
        );
  }

  report_backward_updateweight_last_transfer.end();
  report_backward_updateweight_history.aggregate(report_backward_updateweight_last_transfer);
}

template <typename DataType, typename DriverClass>
DropoutBridge<DataType, Layout_CRDB, DataType, Layout_CRDB, DriverClass>::~DropoutBridge() {
  delete mask_cube;
}

#endif
