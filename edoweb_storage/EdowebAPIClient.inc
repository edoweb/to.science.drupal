<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

class EdowebAPIClient implements EdowebAPIClientInterface {

  public function __construct() {
    $this->__edoweb_api_host = variable_get('edoweb_api_host');
  }

  /*
   * Load an entity from the API.
   *
   * @param $entity
   *   The drupal entity to load data into
   */
  public function load($remote_id, $rdf_model = null) {
    $linkedDataClient = new LinkedDataClient();
    if (! $entity = $linkedDataClient->load($remote_id, $rdf_model)) {
      return false;
    }
    $namespace = variable_get('edoweb_api_namespace');
    return $entity;
  }

  public function getTransformers($entity) {
    $http_url = sprintf(
      '%s/resource/%s.regal',
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $http_response = $this->_http_get($http_url, 'application/json');
    $json = json_decode($http_response->data);
    if ($json && isset($json->transformer)) {
      return $json->transformer;
    }
    return array();
  }

  public function getTemplates($bundle) {
    $http_url = sprintf(
      '%s/resource?namespace=template&contentType=%s&from=0&until=100',
      $this->__edoweb_api_host,
      $bundle
    );
    $http_response = $this->_http_get($http_url, 'application/json');
    return $http_response->data;
  }

  public function getLastModified($remote_id, $content_type = 'file') {
    $http_url = sprintf(
      '%s/resource/%s/all/lastModified?contentType=%s',
      $this->__edoweb_api_host,
      $remote_id,
      $content_type
    );
    $http_response = $this->_http_get($http_url, 'application/json');

    $options = array(
      'format' => 'application/ntriples',
    );
    $rdf_parser = new LibRDF_Parser('ntriples');
    $rdf_model = new LibRDF_Model(new LibRDF_Storage());
    $json = json_decode($http_response->data);
    try {
      $rdf_ntriples = jsonld_to_rdf($json, $options);
    } catch(JsonLdException $e) {
      watchdog('edoweb_storage', 'Unable to parse JSON LD');
      die(FALSE);
    }
    $rdf_model->loadStatementsFromString(
      $rdf_parser,
      $rdf_ntriples
    );

    $entity = current(entity_get_controller('edoweb_basic')->load(
      array($json->{'@id'}), array(), $rdf_model
    ));

    die(l($entity->label(), 'resource/' . $entity->identifier()));

  }

  public function getTree($entity) {
    $http_url = sprintf(
      '%s/resource/%s/all',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_get($http_url, 'application/json');
    return json_decode($http_response->data, TRUE);
  }
  
  public function getMetadata($entity) {
  	$http_url = sprintf(
  			'%s/resource/%s.rdf',
  			$this->__edoweb_api_host,
  			$entity->remote_id
  			);
  	$http_response = $this->_http_get($http_url, 'application/rdf+xml');
  	return $http_response->data;
  }

  public function addURN($entity) {
    list($namespace, $id) = explode(':', $entity->remote_id);
    $http_url = sprintf(
      '%s/utils/addUrn?namespace=%s&id=%s&snid=%s',
      $this->__edoweb_api_host,
      $namespace,
      $id,
      variable_get('urn_namespace', 'urn:uuid')
    );
    return $this->_http_post($http_url);
  }

  public function addDOI($entity) {
    $http_url = sprintf(
      '%s/resource/%s/doi',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    return $this->_http_post($http_url);
  }

  /*
   * Save an entity to the API
   *
   * @param $entity
   *   The drupal entity to store data from
   *
   */
  public function save($entity) {
    $this->saveResource($entity);
    // Store attached files first, as entity metadata has to be
    // updated with corresponding new file URIs
    $this->saveFiles($entity);
    $this->saveMetadata($entity);
  }

  public function saveMetadata($entity) {
    // Send metadata
    $http_post_data = _edoweb_storage_entity_serialize_ntriples(
    		$entity, array('descriptive')
   );
    $http_url = sprintf(
      '%s/resource/%s/metadata',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    return $this->_http_post($http_url, $http_post_data);
  }

  public function saveFiles($entity) {
    $http_url = sprintf(
      '%s/resource/%s/data',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    if (property_exists($entity, 'field_edoweb_datastream')
        && isset($entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'])) {
      $file_url = $entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'];
    }
    if (isset($file_url) && 'public://' == substr($file_url, 0, 9)) {
      $file_name = $entity->field_edoweb_label[LANGUAGE_NONE][0]['value'];
      $file_mime = $entity->field_edoweb_filetype[LANGUAGE_NONE][0]['value'];
      $file_path = drupal_realpath($file_url);
      if ($this->_http_multipart_post("$http_url", $file_path, $file_mime, $file_name)) {
        unset($entity->field_edoweb_datastream[LANGUAGE_NONE][0]);
        unlink($file_path);
      } else {
        watchdog('edoweb_storage', 'Unable to upload file to API');
        return FALSE;
      }
    }
    return TRUE;
  }

  public function getFile($entity) {
    $http_url = sprintf(
      '%s/resource/%s/data',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    return $this->_http_get($http_url);
  }

  public function setParts($entity, $parts) {
    if (isset($entity->remote_id)) {
      // Update resource parts
      $http_url = sprintf(
        '%s/resource/%s/parts',
        $this->__edoweb_api_host,
        $entity->remote_id
      );
      return $this->_http_put($http_url, json_encode($parts), 'application/json');
    }
  }

  public function saveResource($entity, $transformers = null) {
    $data = array();
    $data['contentType'] = $entity->bundle_type;
    if ($transformers) $data['transformer'] = $transformers;
    if (isset($entity->access_data)) $data['accessScheme'] = $entity->access_data;
    if (isset($entity->access_md)) $data['publishScheme'] = $entity->access_md;
    if (isset($entity->uid)) $data['isDescribedBy']['createdBy'] = $entity->uid;
    if (isset($entity->name)) $data['isDescribedBy']['name'] = $entity->name;

    $parents = field_get_items('edoweb_basic', $entity, 'field_edoweb_struct_parent');
    if (FALSE !== $parents) {
      $data['parentPid'] = $parents[0]['value'];
    }
    if (isset($entity->remote_id)) {

      // Update resource
      $http_url = sprintf(
        '%s/resource/%s',
        $this->__edoweb_api_host,
        $entity->remote_id
      );
      $http_response = $this->_http_put($http_url, json_encode($data), 'application/json');

    } else {

      $entity_info = (entity_get_info(EDOWEB_ENTITY_TYPE));
      if (isset($entity->namespace)) {
        $namespace = $entity->namespace;
      } else if (isset($entity_info['bundles'][$entity->bundle()]['namespace'])) {
        $namespace = $entity_info['bundles'][$entity->bundle()]['namespace'];
      } else {
        $namespace = variable_get('edoweb_api_namespace');
      }

      // Create resource
      $http_url = sprintf(
        '%s/resource/%s',
        $this->__edoweb_api_host,
        $namespace
      );
      $http_response = $this->_http_post($http_url, json_encode($data), 'application/json');
      $resource_uri = $http_response->headers['location'];
      $remote_id = substr($resource_uri, strrpos($resource_uri, '/') + 1);
      $entity->remote_id = $remote_id;

    }
    
    return $http_response;

  }

  /*
   * Delete an entity via the API
   *
   * @param $entity
   *   The drupal entity to delete
   *
   */
  public function delete($entity) {
    // Delete resource
    $http_url = sprintf(
      '%s/resource/%s',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    return $this->_http_delete($http_url);
  }

  public function index($entity) {
    // Index resource
    $http_url = sprintf(
      '%s/utils/index/%s?contentType=%s',
      $this->__edoweb_api_host,
      $entity->remote_id,
      $entity->bundle_type
    );
    return $this->_http_post($http_url);
  }

  public function query(EntityFieldQuery $efq) {
    static $query_count = 0;
    $page = pager_find_page($query_count);
    $efq->pager_num = $query_count;

    if (isset($efq->tags['elasticsearch'])) {
      $results = $this->_query_elasticsearch($efq, $page);
    } else if (isset($efq->tags['lobid'])) {
      $results = $this->_query_lobid($efq, $page);
    }

    $results_per_page = variable_get('edoweb_storage_results_per_page', 10);
    pager_default_initialize($results['hits'], $results_per_page, $query_count);
    $query_count++;
    return $results;
  }

  public function setAccessRights($entity, $subtree_apply = false) {

    $data = array();

    if (isset($entity->access_data)) $data['accessScheme'] = $entity->access_data;
    if (isset($entity->access_md)) $data['publishScheme'] = $entity->access_md;

    // Update resource
    $http_url = sprintf(
      '%s/resource/%s',
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    if ($subtree_apply) {
      $http_url .= '/all';
    } else {
      $data['contentType'] = $entity->bundle_type;
    }

    return $this->_http_patch($http_url, json_encode($data), 'application/json');

  }

  public function getStatus($entity) {

    $http_url = sprintf(
      '%s/resource/%s/status',
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $http_response = $this->_http_get($http_url);
    return json_decode($http_response->data, TRUE);

  }

  public function setCrawlerConfiguration($entity, $conf) {

    $http_url = sprintf(
      '%s/resource/%s/conf',
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    return $this->_http_put($http_url, json_encode($conf), 'application/json');

  }

  public function getCrawlerConfiguration($entity) {

    $http_url = sprintf(
      '%s/resource/%s/conf',
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $http_response = $this->_http_get($http_url);
    return json_decode($http_response->data, TRUE);

  }

  public function triggerCrawl($entity) {

    $http_url = sprintf(
      '%s/resource/%s/createVersion',
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    return $this->_http_post($http_url);
    
  }
  public function getView($entity,$style) {
    	      $http_url = sprintf(
  		  	      		'%s/resource/%s/all?design=%s',
  		  	      		$this->__edoweb_api_host,
  		  	      		$entity->remote_id,
  			 	      		$style
  			 	      		);
  	 	      $http_response = $this->_http_get($http_url, 'text/html');
  	 	      return $http_response->data;
  }
  protected function _query_lobid(EntityFieldQuery $efq, $page = 0) {
    $lobid_host = variable_get('edoweb_lobid_host');
    $term = str_replace(
      ' ', '+', $efq->metaData['term']
    );
    $endpoint = $efq->metaData['endpoint'];
    $parameter = $efq->metaData['parameter'];

    $results_per_page = variable_get('edoweb_storage_results_per_page', 10);
    $from = array_key_exists('start', $efq->range)
      ? $efq->range['start']
      : $page * $results_per_page;
    $size = array_key_exists('length', $efq->range)
      ? $efq->range['length']
      : $results_per_page;
    $types = array_key_exists('type', $efq->metaData)
      ? $efq->metaData['type']
      : null;

    $http_url = sprintf(
      '%s/%s?%s=%s&from=%s&size=%s',
      $lobid_host, $endpoint, $parameter, urlencode($term), $from, $size
    );

    if ($types) {
      $bundle_infos = field_info_bundles(EDOWEB_ENTITY_TYPE);
      $rdf_types = array();

      foreach ($types as $type) {
        $bundle_info = $bundle_infos[$type];
        foreach ($bundle_info['rdf_mapping']['type'] as $rdf_type) {
          $rdf_types[] = _edoweb_expand_curie($rdf_type);
        }
      }
      $http_url .= '&type=' . urlencode(implode(',', $rdf_types));
    }

    $http_response = $this->_http_get(
      $http_url, 'text/plain'
    );

    if (200 != $http_response->code || !isset($http_response->data)) {
      watchdog('edoweb_storage', 'Unable to execute query on @api.',
        array(
          '@api' => $lobid_host,
        )
      );
      return FALSE;
    }

    $rdf_parser = new LibRDF_Parser('ntriples');
    $rdf_model = new LibRDF_Model(new LibRDF_Storage());
    try {
      $rdf_model->loadStatementsFromString(
        $rdf_parser,
        $http_response->data
      );
    } catch (LibRDF_Error $e) {
      return FALSE;
    }

    $ontology_model = new LibRDF_Model(new LibRDF_Storage());
    $ontology_model->loadStatementsFromUri(
      new LibRDF_Parser('rdfxml'), 'file://' . __DIR__ . '/../edoweb/gnd.rdf'
    );
    $rdfs_reasoner = new LibRDF_Reasoner('rdfs');
    $rdf_model->inferStatements($rdfs_reasoner, $ontology_model);

    // Retrieve hit count
    $search_total_results = new LibRDF_URINode(
      'http://sindice.com/vocab/search#totalResults'
    );
    try {
      //FIXME: why does query URL as subject not work?
      //$hits = $rdf_model->getTarget(
      //  new LibRDF_URINode($http_url), $search_total_results
      //)->getValue();
      $hits = $rdf_model->findStatements(
        null, $search_total_results, null
      )->current()->getObject()->getValue();
    } catch (LibRDF_LookupError $e) {
      $hits = 0;
    }
    $results = array(
      'hits' => $hits
    );

    // Retrieve primary topics, i.e. actual result resources
    $foaf_primary_topic = new LibRDF_URINode(
      'http://xmlns.com/foaf/0.1/primaryTopic'
    );
    $primary_topics = $rdf_model->findStatements(
      null, $foaf_primary_topic, null
    );
    $remote_ids = array();
    foreach ($primary_topics as $primary_topic) {
      $remote_ids[] = $primary_topic->getObject()->getValue();
    }

    // Load RDF into entities
    $results[EDOWEB_ENTITY_TYPE] = entity_get_controller('edoweb_basic')->load(
      $remote_ids, array(), $rdf_model
    );

    return $results;

  }

  protected function _query_elasticsearch(EntityFieldQuery $efq, $page = 0) {

    $index = user_access('edit any edoweb_basic entity')
      ? variable_get('edoweb_api_namespace')
      : 'public_' . variable_get('edoweb_api_namespace');

    if (isset($efq->tags['fulltext'])) {
      $index .= ',fulltext_' . variable_get('edoweb_api_namespace');
    }

    // Search resources
    $http_url = sprintf(
      '%s/search/%s/_search',
      $this->__edoweb_api_host,
      $index
    );

    $query = $this->_efq_to_es($efq, $page);
    $http_response = $this->_http_post(
      $http_url, $query, 'application/json'
    );

    if (200 != $http_response->code || !isset($http_response->data)) {
      return array();
    }

    $json_response = json_decode($http_response->data);
    $hits = $json_response->hits;
    $facets = isset($json_response->facets)
      ? $json_response->facets : array();

    $results = array(
      'hits' => $hits->total,
      'facets' => $facets,
      EDOWEB_ENTITY_TYPE => array(),
    );

    // Parse results
    $options = array(
      'format' => 'application/ntriples',
    );
    $rdf_parser = new LibRDF_Parser('ntriples');
    $rdf_model = new LibRDF_Model(new LibRDF_Storage());

    foreach ($hits->hits as $hit) {
      try {
        $rdf_ntriples = jsonld_to_rdf($hit->_source, $options);
      } catch(JsonLdException $e) {
        watchdog('edoweb_storage', 'Unable to parse JSON LD: ' .  json_encode($hit->_source));
        continue;
      }
      $rdf_model->loadStatementsFromString(
        $rdf_parser,
        $rdf_ntriples
      );
    }

    // Identify primary resources
    $ore_describes = $rdf_model->findStatements(
      null,
      new LibRDF_URINode(
        "http://xmlns.com/foaf/0.1/primaryTopic"
      ),
      null
    );
    $remote_ids = array();
    foreach ($ore_describes as $stmt) {
      $remote_ids[] = $stmt->getObject()->getValue();
    }

    // Load RDF into entities
    $results[EDOWEB_ENTITY_TYPE] = entity_get_controller('edoweb_basic')->load(
      $remote_ids, array(), $rdf_model
    );

    return $results;

  }

  protected function _efq_to_es(EntityFieldQuery $efq, $page = 0) {
    $query = array();
    $results_per_page = variable_get('edoweb_storage_results_per_page', 10);
    $query['from'] = array_key_exists('start', $efq->range)
      ? $efq->range['start']
      : $page * $results_per_page;
    $query['size'] = array_key_exists('length', $efq->range)
      ? $efq->range['length']
      : $results_per_page;

    if (array_key_exists('term', $efq->metaData)) {
      $query['query']['filtered']['query']['query_string']['query']
        = $efq->metaData['term'];
      $query['query']['filtered']['query']['query_string']['default_operator']
        = 'AND';
    }

    if (array_key_exists('bundle', $efq->entityConditions)) {
      $target_bundles = $efq->entityConditions['bundle']['value'];
      if (!is_array($target_bundles)) {
        $target_bundles = array($target_bundles);
      }
      $query['query']['filtered']['filter']['bool']['must'][]['terms']["contentType"]
        = array_values($target_bundles);
    }

    foreach($efq->propertyConditions as $property_condition) {
      // TODO: Handle via RDF mapping
      switch ($property_condition['column']) {
        case 'uid':
          $uids = $property_condition['value'];
          if (!is_array($uids)) {
            $uids = array($uids);
          }
          $query['query']['filtered']['filter']['bool']['must'][]['terms']
            ['isDescribedBy.createdBy'] = $uids;
          break;
        case 'mid':
          $mids = $property_condition['value'];
          if (!is_array($mids)) {
            $mids = array($mids);
          }
          $query['query']['filtered']['filter']['bool']['must'][]['terms']
            ['isDescribedBy.lastModifiedBy'] = $mids;
          break;
      }
    }

    foreach($efq->fieldConditions as $field_condition) {
      $bool_op = empty($field_condition['delta_group'])
        ? 'and' : $field_condition['delta_group'];
      $field_name = $field_condition['field']['field_name'];
      $field_info = field_info_field($field_name);
      $json_ld_property = _field_name_to_jsonld_key($field_name);
      $field_value = $field_condition['value'];
      if ($bool_op == 'and') {
        $query['query']['filtered']['filter']['bool']['must'][]['term'][$json_ld_property] = "$field_value";
      } else if ($bool_op == 'or') {
        $query['query']['filtered']['filter']['bool']['should'][]['term'][$json_ld_property] = "$field_value";
      }
    }

    if (!isset($query['query'])) {
      $query['query'] = array("match_all" => new stdClass());
    }

    $query['sort'] = array();
    foreach($efq->order as $sort) {
      switch ($sort['type']) {
        case 'property':
          $property_name = $sort['specifier'];
          if ($property_name == 'updated') {
            $jsonld_property = 'modified';
          } else if ($property_name == 'objectTimestamp') {
            $jsonld_property = 'objectTimestamp';
          }
          break;
        case 'field':
          $field_name = $sort['specifier']['field']['field_name'];
          $jsonld_property = _field_name_to_jsonld_key($field_name);
          break;
        default:
          break;
      }
      $order = $sort['direction'];
      if (isset($jsonld_property) && $jsonld_property) {
        $query['sort'][][$jsonld_property] = array(
          'order' => $order,
          'ignore_unmapped' => TRUE,
        );
      }
    }

    $facet_size = array_key_exists('facet_size', $efq->metaData) ? $efq->metaData['facet_size'] : 10;
    $facet_order = array_key_exists('facet_order', $efq->metaData) ? $efq->metaData['facet_order'] : 'count';
    foreach (variable_get('available_facets') as $available_facet => $enabled) {
      if ($enabled) {
        $facet_field = explode('.', $available_facet);
        $field_name = current($facet_field);
        if (next($facet_field) == '@id') {
          $query['facets'][$field_name]['terms']['field'] = $field_name.'.prefLabel';
        } else {
          $query['facets'][$field_name]['terms']['field'] = $available_facet;
        }
        $query['facets'][$field_name]['terms']['size'] = $facet_size;
        $query['facets'][$field_name]['terms']['order'] = $facet_order;
      }
    }

    global $user;
    if (in_array('edoweb_backend_admin', $user->roles)
        || in_array('edoweb_backend_user', $user->roles)) {
      $query['facets']['createdBy']['terms'] = array(
        'field' => 'isDescribedBy.createdBy',
        'size' => 99,
      );
      $query['facets']['lastModifiedBy']['terms'] = array(
        'field' => 'isDescribedBy.lastModifiedBy',
        'size' => 99,
      );
    }
    return json_encode($query);
  }

  protected function _http_multipart_post($request_url, $file_path, $content_type = null, $file_name = null) {

    $request = curl_init($request_url);

    $params = array('md5' => md5_file($file_path));
    $files = array(
      'data' => array(
        'filepath' => $file_path,
        'filename' => $file_name,
        'filetype' => $content_type,
      )
    );
    $postfields = $this->_curl_custom_postfields($params, $files);

    curl_setopt_array($request, $postfields);

    $response = curl_exec($request);
    $info = curl_getinfo($request);
    $curl_error = curl_errno($request);
    $response_code = $info['http_code'];

    if (417 == $response_code) {
      $admin_mail = variable_get('site_mail', ini_get('sendmail_from'));
      drupal_set_message(t("
        Beim Übertragen der Datei ist ein Problem aufgetreten. Bitte
        laden Sie die Datei erneut hoch. Sollte das Problem weiterhin
        bestehen, wenden Sie sich an <a href=\"mailto:$admin_mail\">
        $admin_mail</a>."), 'error'
      );
      return FALSE;
    } else if ($curl_error || 200 != $response_code) {
      watchdog('edoweb_storage', 'Error uploading file @file to @api.',
        array(
          '@file' => $file_path,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return FALSE;
    } else {
      return TRUE;
    }

  }

  /**
  * For safe multipart POST request for PHP5.3 ~ PHP 5.4.
  * based on http://php.net/manual/en/class.curlfile.php#115161
  *
  * @param array $assoc "name => value"
  * @param array $files "name => path"
  * @return bool
  */
  protected function _curl_custom_postfields(array $assoc = array(), array $files = array()) {

    // invalid characters for "name" and "filename"
    static $disallow = array("\0", "\"", "\r", "\n");

    // build normal parameters
    foreach ($assoc as $k => $v) {
      $k = str_replace($disallow, "_", $k);
      $body[] = implode("\r\n", array(
        "Content-Disposition: form-data; name=\"{$k}\"",
        "",
        filter_var($v),
      ));
    }

    // build file parameters
    foreach ($files as $k => $file) {
      $path = $file['filepath'];
      $name = $file['filename'];
      $type = $file['filetype'];
      switch (true) {
        case false === $path = realpath(filter_var($path)):
        case !is_file($path):
        case !is_readable($path):
          continue; // or return false, throw new InvalidArgumentException
      }
      $data = file_get_contents($path);
      $path = call_user_func("end", explode(DIRECTORY_SEPARATOR, $path));
      $k = str_replace($disallow, "_", $k);
      $path = str_replace($disallow, "_", $path);
      $body[] = implode("\r\n", array(
        "Content-Disposition: form-data; name=\"{$k}\"; filename=\"{$name}\"",
        "Content-Type: $type",
        "",
        $data,
      ));
    }

    // generate safe boundary
    do {
      $boundary = "---------------------" . md5(mt_rand() . microtime());
    } while (preg_grep("/{$boundary}/", $body));

    // add boundary for each parameters
    array_walk($body, function (&$part) use ($boundary) {
      $part = "--{$boundary}\r\n{$part}";
    });

    // add final boundary
    $body[] = "--{$boundary}--";
    $body[] = "";

    // set options
    return array(
      CURLOPT_POST => TRUE,
      CURLOPT_POSTFIELDS => implode("\r\n", $body),
      CURLOPT_RETURNTRANSFER => TRUE,
      CURLOPT_HTTPHEADER => array(
        "Expect: 100-continue",
        "Content-Type: multipart/form-data; boundary={$boundary}", // change Content-Type
        "Authorization: " . _get_api_auth_header(),
        "UserIp: " . $_SERVER['REMOTE_ADDR'],
      ),
    );

  }


  protected function _http_put($request_url, $request_body = '', $content_type='text/plain') {
    $http_options = array(
      'method' => 'PUT',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_post($request_url, $request_body = '', $content_type='text/plain') {
    $http_options = array(
      'method' => 'POST',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_get($request_url, $content_type = 'text/plain;charset=utf-8') {
    $http_options = array(
      'method' => 'GET',
      'headers' => array('Accept' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_delete($request_url) {
    $http_options = array(
      'method' => 'DELETE',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_head($request_url) {
    $http_options = array(
      'method' => 'HEAD',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_patch($request_url, $request_body, $content_type = 'text/plain') {
    $http_options = array(
      'method' => 'PATCH',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

}

